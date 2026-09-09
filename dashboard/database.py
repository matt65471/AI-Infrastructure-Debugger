"""PostgreSQL persistence, OTLP ingestion, and rollup queries."""

from __future__ import annotations

import hashlib
import threading
import uuid
from collections.abc import Iterable
from datetime import UTC, date, datetime, timedelta
from decimal import Decimal
from pathlib import Path
from typing import Any

from opentelemetry.proto.collector.trace.v1.trace_service_pb2 import (
    ExportTraceServiceRequest,
)
from psycopg import sql
from psycopg.rows import dict_row
from psycopg.types.json import Jsonb
from psycopg_pool import ConnectionPool


RESOURCE_NAMESPACE = uuid.UUID("b1a88c65-1044-4de1-b8c4-86fd2621db31")
MIGRATION_LOCK_ID = 743_583_219
PARTITION_LOCK_ID = 743_583_220

RESOURCE_NODE = 1
RESOURCE_DEPLOYMENT = 2
RESOURCE_POD = 3
RESOURCE_CONTAINER = 4
RESOURCE_PROCESS = 5


def utc_from_millis(value: int) -> datetime:
    return datetime.fromtimestamp(value / 1000, tz=UTC)


def utc_from_nanos(value: int) -> datetime:
    return datetime.fromtimestamp(value / 1_000_000_000, tz=UTC)


def parse_timestamp(value: str | None, fallback: datetime) -> datetime:
    if not value:
        return fallback
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return fallback


def protobuf_value(value: Any) -> Any:
    kind = value.WhichOneof("value")
    if kind == "string_value":
        return value.string_value
    if kind == "bool_value":
        return value.bool_value
    if kind == "int_value":
        return value.int_value
    if kind == "double_value":
        return value.double_value
    if kind == "bytes_value":
        return value.bytes_value.hex()
    if kind == "array_value":
        return [protobuf_value(item) for item in value.array_value.values]
    if kind == "kvlist_value":
        return {
            item.key: protobuf_value(item.value)
            for item in value.kvlist_value.values
        }
    return None


def protobuf_attributes(attributes: Iterable[Any]) -> dict[str, Any]:
    return {item.key: protobuf_value(item.value) for item in attributes}


def resource_uuid(kind: int, *parts: Any) -> uuid.UUID:
    canonical = ":".join(str(part or "") for part in (kind, *parts))
    return uuid.uuid5(RESOURCE_NAMESPACE, canonical)


class TelemetryDatabase:
    def __init__(self, database_url: str, migrations_dir: Path):
        self.database_url = database_url
        self.migrations_dir = migrations_dir
        self.pool = ConnectionPool(
            conninfo=database_url,
            min_size=1,
            max_size=5,
            open=False,
            kwargs={"row_factory": dict_row},
        )
        self.ready = False
        self._known_partitions: set[tuple[str, date]] = set()
        self._partition_cache_lock = threading.Lock()

    def open(self, timeout: float = 10.0) -> None:
        if self.ready:
            return
        if self.pool.closed:
            self.pool.open()
        self.pool.wait(timeout=timeout)
        self.run_migrations()
        self.ensure_partition("spans", datetime.now(UTC).date())
        self.ensure_partition("infra_samples", datetime.now(UTC).date())
        self.ready = True

    def close(self) -> None:
        self.pool.close()
        self.ready = False

    def run_migrations(self) -> None:
        with self.pool.connection() as connection:
            with connection.transaction():
                connection.execute(
                    "SELECT pg_advisory_xact_lock(%s)", (MIGRATION_LOCK_ID,)
                )
                connection.execute("CREATE SCHEMA IF NOT EXISTS telemetry")
                connection.execute(
                    """
                    CREATE TABLE IF NOT EXISTS telemetry.schema_migrations (
                        version text PRIMARY KEY,
                        applied_at timestamptz NOT NULL DEFAULT now()
                    )
                    """
                )
                applied = {
                    row["version"]
                    for row in connection.execute(
                        "SELECT version FROM telemetry.schema_migrations"
                    ).fetchall()
                }
                for migration in sorted(self.migrations_dir.glob("*.sql")):
                    if migration.name in applied:
                        continue
                    connection.execute(migration.read_text(encoding="utf-8"))
                    connection.execute(
                        "INSERT INTO telemetry.schema_migrations (version) VALUES (%s)",
                        (migration.name,),
                    )

    def ensure_partition(self, table: str, day: date) -> None:
        if table not in {"spans", "infra_samples"}:
            raise ValueError(f"unsupported partitioned table: {table}")
        cache_key = (table, day)
        if cache_key in self._known_partitions:
            return
        with self._partition_cache_lock:
            if cache_key in self._known_partitions:
                return
            partition = f"{table}_{day:%Y%m%d}"
            following_day = day + timedelta(days=1)
            with self.pool.connection() as connection:
                with connection.transaction():
                    connection.execute(
                        "SELECT pg_advisory_xact_lock(%s)", (PARTITION_LOCK_ID,)
                    )
                    connection.execute(
                        sql.SQL(
                            "CREATE TABLE IF NOT EXISTS telemetry.{} "
                            "PARTITION OF telemetry.{} FOR VALUES FROM ({}) TO ({})"
                        ).format(
                            sql.Identifier(partition),
                            sql.Identifier(table),
                            sql.Literal(day),
                            sql.Literal(following_day),
                        )
                    )
            self._known_partitions.add(cache_key)

    def _upsert_resource(
        self,
        connection: Any,
        *,
        key: uuid.UUID,
        kind: int,
        observed_at: datetime,
        service_name: str | None = None,
        node_name: str | None = None,
        namespace_name: str | None = None,
        deployment_name: str | None = None,
        kubernetes_uid: str | None = None,
        pod_uid: str | None = None,
        pod_name: str | None = None,
        container_id: str | None = None,
        container_name: str | None = None,
        parent_resource_id: int | None = None,
        attributes: dict[str, Any] | None = None,
    ) -> int:
        row = connection.execute(
            """
            INSERT INTO telemetry.resources (
                resource_key, kind, service_name, node_name, namespace_name,
                deployment_name, kubernetes_uid, pod_uid, pod_name,
                container_id, container_name, parent_resource_id,
                first_seen_at, last_seen_at, attributes
            ) VALUES (
                %(key)s, %(kind)s, %(service_name)s, %(node_name)s,
                %(namespace_name)s, %(deployment_name)s, %(kubernetes_uid)s,
                %(pod_uid)s, %(pod_name)s, %(container_id)s,
                %(container_name)s, %(parent_resource_id)s,
                %(observed_at)s, %(observed_at)s, %(attributes)s
            )
            ON CONFLICT (resource_key) DO UPDATE SET
                service_name = COALESCE(EXCLUDED.service_name, resources.service_name),
                node_name = COALESCE(EXCLUDED.node_name, resources.node_name),
                namespace_name = COALESCE(EXCLUDED.namespace_name, resources.namespace_name),
                deployment_name = COALESCE(EXCLUDED.deployment_name, resources.deployment_name),
                kubernetes_uid = COALESCE(EXCLUDED.kubernetes_uid, resources.kubernetes_uid),
                pod_uid = COALESCE(EXCLUDED.pod_uid, resources.pod_uid),
                pod_name = COALESCE(EXCLUDED.pod_name, resources.pod_name),
                container_id = COALESCE(EXCLUDED.container_id, resources.container_id),
                container_name = COALESCE(EXCLUDED.container_name, resources.container_name),
                parent_resource_id = COALESCE(EXCLUDED.parent_resource_id, resources.parent_resource_id),
                last_seen_at = GREATEST(resources.last_seen_at, EXCLUDED.last_seen_at),
                attributes = resources.attributes || EXCLUDED.attributes
            RETURNING id
            """,
            {
                "key": key,
                "kind": kind,
                "service_name": service_name or None,
                "node_name": node_name or None,
                "namespace_name": namespace_name or None,
                "deployment_name": deployment_name or None,
                "kubernetes_uid": kubernetes_uid or None,
                "pod_uid": pod_uid or None,
                "pod_name": pod_name or None,
                "container_id": container_id or None,
                "container_name": container_name or None,
                "parent_resource_id": parent_resource_id,
                "observed_at": observed_at,
                "attributes": Jsonb(attributes or {}),
            },
        ).fetchone()
        return int(row["id"])

    def ingest_snapshot(self, snapshot: dict[str, Any]) -> None:
        node = snapshot["node"]
        sampled_at = utc_from_millis(int(node["timestamp_unix_ms"]))
        self.ensure_partition("infra_samples", sampled_at.date())
        hostname = str(node.get("hostname", "unknown"))

        with self.pool.connection() as connection:
            with connection.transaction():
                sample_rows: list[dict[str, Any]] = []
                node_id = self._upsert_resource(
                    connection,
                    key=resource_uuid(RESOURCE_NODE, hostname),
                    kind=RESOURCE_NODE,
                    observed_at=sampled_at,
                    node_name=hostname,
                    attributes={"logical_cpu_count": node.get("logical_cpu_count")},
                )
                sample_rows.append(
                    self._sample_row(sampled_at, node_id, node, "node")
                )

                deployments: dict[tuple[str, str], int] = {}
                for deployment in snapshot.get("deployments", []):
                    namespace = deployment.get("namespace", "")
                    name = deployment.get("deployment_name", "")
                    deployment_id = self._upsert_resource(
                        connection,
                        key=resource_uuid(
                            RESOURCE_DEPLOYMENT,
                            namespace,
                            deployment.get("deployment_uid") or name,
                        ),
                        kind=RESOURCE_DEPLOYMENT,
                        observed_at=sampled_at,
                        service_name=name,
                        node_name=hostname,
                        namespace_name=namespace,
                        deployment_name=name,
                        kubernetes_uid=deployment.get("deployment_uid"),
                        parent_resource_id=node_id,
                    )
                    deployments[(namespace, name)] = deployment_id
                    sample_rows.append(
                        self._sample_row(
                            sampled_at, deployment_id, deployment, "deployment"
                        )
                    )

                pods: dict[str, int] = {}
                for pod in snapshot.get("pods", []):
                    pod_uid = pod.get("pod_uid", "")
                    namespace = pod.get("namespace", "")
                    workload = pod.get("workload_name", "")
                    pod_id = self._upsert_resource(
                        connection,
                        key=resource_uuid(RESOURCE_POD, namespace, pod_uid),
                        kind=RESOURCE_POD,
                        observed_at=sampled_at,
                        service_name=workload,
                        node_name=pod.get("node_name") or hostname,
                        namespace_name=namespace,
                        deployment_name=workload if pod.get("workload_kind") == "Deployment" else None,
                        kubernetes_uid=pod_uid,
                        pod_uid=pod_uid,
                        pod_name=pod.get("pod_name"),
                        parent_resource_id=deployments.get((namespace, workload), node_id),
                    )
                    pods[pod_uid] = pod_id
                    sample_rows.append(
                        self._sample_row(sampled_at, pod_id, pod, "pod")
                    )

                for container in snapshot.get("containers", []):
                    pod_uid = container.get("pod_uid", "")
                    container_name = container.get("container_name", "")
                    container_id = container.get("container_id", "")
                    if pod_uid and container_name:
                        key = resource_uuid(RESOURCE_CONTAINER, pod_uid, container_name)
                    else:
                        key = resource_uuid(RESOURCE_CONTAINER, hostname, container_id)
                    container_resource_id = self._upsert_resource(
                        connection,
                        key=key,
                        kind=RESOURCE_CONTAINER,
                        observed_at=sampled_at,
                        service_name=container.get("workload_name") or container_name,
                        node_name=container.get("node_name") or hostname,
                        namespace_name=container.get("namespace"),
                        deployment_name=(
                            container.get("workload_name")
                            if container.get("workload_kind") == "Deployment"
                            else None
                        ),
                        pod_uid=pod_uid,
                        pod_name=container.get("pod_name"),
                        container_id=container_id,
                        container_name=container_name,
                        parent_resource_id=pods.get(pod_uid, node_id),
                        attributes={"image": container.get("image")},
                    )
                    sample_rows.append(
                        self._sample_row(
                            sampled_at,
                            container_resource_id,
                            container,
                            "container",
                        )
                    )

                top_processes = {
                    int(process["pid"]): process
                    for process in (
                        snapshot.get("top_cpu_processes", [])
                        + snapshot.get("top_memory_processes", [])
                    )
                }
                for process in top_processes.values():
                    process_id = self._upsert_resource(
                        connection,
                        key=resource_uuid(
                            RESOURCE_PROCESS,
                            hostname,
                            process.get("pid"),
                            process.get("name"),
                        ),
                        kind=RESOURCE_PROCESS,
                        observed_at=sampled_at,
                        node_name=hostname,
                        parent_resource_id=node_id,
                        attributes={
                            "pid": process.get("pid"),
                            "process_name": process.get("name"),
                        },
                    )
                    sample_rows.append(
                        self._sample_row(
                            sampled_at, process_id, process, "process"
                        )
                    )

                for event in snapshot.get("kubernetes_events", []):
                    self._upsert_event(connection, event, sampled_at)
                self._insert_samples(connection, sample_rows)

    def _sample_row(
        self,
        sampled_at: datetime,
        resource_id: int,
        data: dict[str, Any],
        kind: str,
    ) -> dict[str, Any]:
        pressure = data.get("pressure") or {}
        memory_bytes = data.get("memory_current_bytes")
        memory_limit = data.get("memory_max_bytes") or data.get("memory_limit_bytes")
        if kind == "node":
            memory_bytes = (
                int(data.get("memory_total_kb", 0))
                - int(data.get("memory_available_kb", 0))
            ) * 1024
            memory_limit = int(data.get("memory_total_kb", 0)) * 1024
        elif kind == "process":
            memory_bytes = int(data.get("resident_memory_kb", 0)) * 1024

        known = {
            "cpu_usage_percent", "cpu_limit_cores", "throttled_usec_delta",
            "memory_current_bytes", "memory_max_bytes", "memory_limit_bytes",
            "memory_usage_percent", "memory_high_delta", "oom_kill_delta",
            "network_rx_bytes_per_second", "network_tx_bytes_per_second",
            "tcp_retransmits_per_second", "disk_read_bytes_per_second",
            "disk_write_bytes_per_second", "restart_count", "ready",
            "pod_ready", "all_containers_ready", "container_ready", "pressure",
        }
        extra = {key: value for key, value in data.items() if key not in known}
        ready = data.get("ready")
        if ready is None:
            ready = data.get("all_containers_ready", data.get("pod_ready", data.get("container_ready")))

        return {
            "sampled_at": sampled_at,
            "resource_id": resource_id,
            "cpu": data.get("cpu_usage_percent"),
            "cpu_limit": data.get("cpu_limit_cores"),
            "throttled": data.get("throttled_usec_delta"),
            "memory": memory_bytes,
            "memory_limit": memory_limit,
            "memory_percent": data.get("memory_usage_percent"),
            "memory_high": data.get("memory_high_delta"),
            "oom_kill": data.get("oom_kill_delta"),
            "network_rx": data.get("network_rx_bytes_per_second"),
            "network_tx": data.get("network_tx_bytes_per_second"),
            "tcp_retransmits": data.get("tcp_retransmits_per_second"),
            "disk_read": data.get("disk_read_bytes_per_second"),
            "disk_write": data.get("disk_write_bytes_per_second"),
            "cpu_pressure": ((pressure.get("cpu") or {}).get("some") or {}).get("avg10"),
            "memory_pressure": ((pressure.get("memory") or {}).get("full") or {}).get("avg10"),
            "io_pressure": ((pressure.get("io") or {}).get("full") or {}).get("avg10"),
            "restarts": data.get("restart_count"),
            "ready": ready,
            "extra": Jsonb(extra),
        }

    def _insert_samples(
        self, connection: Any, sample_rows: list[dict[str, Any]]
    ) -> None:
        if not sample_rows:
            return
        with connection.cursor() as cursor:
            cursor.executemany(
                """
                INSERT INTO telemetry.infra_samples (
                    sampled_at, resource_id, cpu_usage_percent, cpu_limit_cores,
                    cpu_throttled_usec_delta, memory_current_bytes,
                    memory_limit_bytes, memory_usage_percent, memory_high_delta,
                    oom_kill_delta, network_rx_bytes_per_second,
                    network_tx_bytes_per_second, tcp_retransmits_delta,
                    disk_read_bytes_per_second, disk_write_bytes_per_second,
                    cpu_pressure_avg10, memory_pressure_full_avg10,
                    io_pressure_full_avg10, restart_count, ready, extra_metrics
                ) VALUES (
                    %(sampled_at)s, %(resource_id)s, %(cpu)s, %(cpu_limit)s,
                    %(throttled)s, %(memory)s, %(memory_limit)s,
                    %(memory_percent)s, %(memory_high)s, %(oom_kill)s,
                    %(network_rx)s, %(network_tx)s, %(tcp_retransmits)s,
                    %(disk_read)s, %(disk_write)s, %(cpu_pressure)s,
                    %(memory_pressure)s, %(io_pressure)s, %(restarts)s,
                    %(ready)s, %(extra)s
                ) ON CONFLICT (sampled_at, resource_id) DO NOTHING
                """,
                sample_rows,
            )

    def _upsert_event(
        self, connection: Any, event: dict[str, Any], fallback: datetime
    ) -> None:
        identity = "|".join(
            str(event.get(key, ""))
            for key in ("namespace", "event_type", "reason", "object_uid", "message")
        )
        event_key = hashlib.sha256(identity.encode()).hexdigest()
        resource = connection.execute(
            "SELECT id FROM telemetry.resources WHERE kubernetes_uid = %s ORDER BY last_seen_at DESC LIMIT 1",
            (event.get("object_uid"),),
        ).fetchone()
        first_seen = parse_timestamp(event.get("first_timestamp"), fallback)
        last_seen = parse_timestamp(event.get("last_timestamp"), fallback)
        connection.execute(
            """
            INSERT INTO telemetry.events (
                event_key, first_seen_at, last_seen_at, resource_id, source,
                severity, reason, message, occurrence_count, details
            ) VALUES (%s, %s, %s, %s, 1, %s, %s, %s, %s, %s)
            ON CONFLICT (event_key) DO UPDATE SET
                last_seen_at = GREATEST(events.last_seen_at, EXCLUDED.last_seen_at),
                resource_id = COALESCE(events.resource_id, EXCLUDED.resource_id),
                occurrence_count = GREATEST(events.occurrence_count, EXCLUDED.occurrence_count),
                details = EXCLUDED.details
            """,
            (
                event_key,
                first_seen,
                last_seen,
                resource["id"] if resource else None,
                2 if event.get("event_type") == "Warning" else 1,
                event.get("reason") or "Unknown",
                event.get("message"),
                int(event.get("count", 1)),
                Jsonb(event),
            ),
        )

    def ingest_otlp_traces(self, payload: bytes) -> int:
        request = ExportTraceServiceRequest.FromString(payload)
        spans_to_insert: list[dict[str, Any]] = []
        days: set[date] = set()

        with self.pool.connection() as connection:
            with connection.transaction():
                for resource_spans in request.resource_spans:
                    resource_attributes = protobuf_attributes(
                        resource_spans.resource.attributes
                    )
                    service = str(resource_attributes.get("service.name") or "unknown")
                    namespace = str(resource_attributes.get("k8s.namespace.name") or "")
                    pod_uid = str(resource_attributes.get("service.instance.id") or "")
                    pod_uid = str(resource_attributes.get("k8s.pod.uid") or pod_uid)
                    pod_name = str(resource_attributes.get("k8s.pod.name") or "")
                    container_name = str(resource_attributes.get("k8s.container.name") or "")
                    node_name = str(resource_attributes.get("k8s.node.name") or "")
                    observed_at = datetime.now(UTC)
                    key = resource_uuid(
                        RESOURCE_CONTAINER,
                        pod_uid or namespace,
                        container_name or service,
                    )
                    resource_id = self._upsert_resource(
                        connection,
                        key=key,
                        kind=RESOURCE_CONTAINER,
                        observed_at=observed_at,
                        service_name=service,
                        node_name=node_name,
                        namespace_name=namespace,
                        deployment_name=service,
                        pod_uid=pod_uid,
                        pod_name=pod_name,
                        container_name=container_name,
                        attributes=resource_attributes,
                    )
                    for scope_spans in resource_spans.scope_spans:
                        for span in scope_spans.spans:
                            attributes = protobuf_attributes(span.attributes)
                            started_at = utc_from_nanos(span.start_time_unix_nano)
                            days.add(started_at.date())
                            spans_to_insert.append(
                                {
                                    "started_at": started_at,
                                    "trace_id": span.trace_id,
                                    "span_id": span.span_id,
                                    "parent_span_id": span.parent_span_id or None,
                                    "resource_id": resource_id,
                                    "operation_name": span.name,
                                    "span_kind": int(span.kind),
                                    "duration_us": max(
                                        0,
                                        (span.end_time_unix_nano - span.start_time_unix_nano)
                                        // 1000,
                                    ),
                                    "status_code": int(span.status.code),
                                    "http_method": attributes.get("http.request.method")
                                    or attributes.get("http.method"),
                                    "http_route": attributes.get("http.route"),
                                    "http_status_code": attributes.get("http.response.status_code")
                                    or attributes.get("http.status_code"),
                                    "error_type": attributes.get("error.type"),
                                    "attributes": Jsonb(attributes),
                                }
                            )

        for day in days:
            self.ensure_partition("spans", day)

        if not spans_to_insert:
            return 0
        with self.pool.connection() as connection:
            with connection.transaction():
                with connection.cursor() as cursor:
                    cursor.executemany(
                        """
                        INSERT INTO telemetry.spans (
                            started_at, trace_id, span_id, parent_span_id,
                            resource_id, operation_name, span_kind, duration_us,
                            status_code, http_method, http_route, http_status_code,
                            error_type, attributes
                        ) VALUES (
                            %(started_at)s, %(trace_id)s, %(span_id)s,
                            %(parent_span_id)s, %(resource_id)s,
                            %(operation_name)s, %(span_kind)s, %(duration_us)s,
                            %(status_code)s, %(http_method)s, %(http_route)s,
                            %(http_status_code)s, %(error_type)s, %(attributes)s
                        ) ON CONFLICT (started_at, trace_id, span_id) DO NOTHING
                        """,
                        spans_to_insert,
                    )
        return len(spans_to_insert)

    def query_overview(self, minutes: int) -> dict[str, Any]:
        end = datetime.now(UTC).replace(second=0, microsecond=0)
        start = end - timedelta(minutes=minutes)
        buckets = [start + timedelta(minutes=index) for index in range(minutes)]
        with self.pool.connection() as connection:
            node_rows = connection.execute(
                """
                SELECT date_trunc('minute', sample.sampled_at) AS bucket,
                       avg(sample.cpu_usage_percent) AS cpu_usage_percent,
                       avg(sample.memory_usage_percent) AS memory_usage_percent
                FROM telemetry.infra_samples sample
                JOIN telemetry.resources resource ON resource.id = sample.resource_id
                WHERE resource.kind = 1 AND sample.sampled_at >= %s AND sample.sampled_at < %s
                GROUP BY 1 ORDER BY 1
                """,
                (start, end),
            ).fetchall()
            rollup_rows = connection.execute(
                """
                SELECT * FROM telemetry.service_rollups_1m
                WHERE bucket >= %s AND bucket < %s AND http_route = ''
                ORDER BY namespace_name, deployment_name, bucket
                """,
                (start, end),
            ).fetchall()

        node_by_bucket = {row["bucket"]: row for row in node_rows}
        node_series = [
            {
                "bucket": bucket.isoformat(),
                "cpu_usage_percent": _float(node_by_bucket.get(bucket), "cpu_usage_percent"),
                "memory_usage_percent": _float(node_by_bucket.get(bucket), "memory_usage_percent"),
            }
            for bucket in buckets
        ]
        grouped: dict[tuple[str, str, str], list[dict[str, Any]]] = {}
        for row in rollup_rows:
            key = (row["namespace_name"], row["deployment_name"], row["service_name"])
            grouped.setdefault(key, []).append(dict(row))
        deployments = []
        for (namespace, deployment, service), rows in grouped.items():
            by_bucket = {row["bucket"]: row for row in rows}
            series = [_rollup_json(bucket, by_bucket.get(bucket)) for bucket in buckets]
            populated = [item for item in series if item["request_count"] is not None]
            deployments.append(
                {
                    "namespace": namespace,
                    "deployment_name": deployment,
                    "service_name": service,
                    "latest": populated[-1] if populated else None,
                    "series": series,
                }
            )
        return {
            "window_minutes": minutes,
            "start": start.isoformat(),
            "end": end.isoformat(),
            "node": node_series,
            "deployments": deployments,
        }

    def query_deployment(
        self, namespace: str, deployment: str, minutes: int
    ) -> dict[str, Any] | None:
        end = datetime.now(UTC).replace(second=0, microsecond=0)
        start = end - timedelta(minutes=minutes)
        buckets = [start + timedelta(minutes=index) for index in range(minutes)]
        with self.pool.connection() as connection:
            known = connection.execute(
                """
                SELECT EXISTS (
                    SELECT 1 FROM telemetry.resources
                    WHERE namespace_name = %s AND deployment_name = %s
                ) AS found
                """,
                (namespace, deployment),
            ).fetchone()["found"]
            if not known:
                return None
            rows = connection.execute(
                """
                SELECT * FROM telemetry.service_rollups_1m
                WHERE namespace_name = %s AND deployment_name = %s
                  AND bucket >= %s AND bucket < %s
                ORDER BY bucket, http_route
                """,
                (namespace, deployment, start, end),
            ).fetchall()

        aggregate = [row for row in rows if row["http_route"] == ""]
        by_bucket = {row["bucket"]: row for row in aggregate}
        latest_bucket = max((row["bucket"] for row in rows), default=None)
        routes = [
            _rollup_json(row["bucket"], row) | {"http_route": row["http_route"]}
            for row in rows
            if row["http_route"] and row["bucket"] == latest_bucket
        ]
        return {
            "namespace": namespace,
            "deployment_name": deployment,
            "window_minutes": minutes,
            "series": [_rollup_json(bucket, by_bucket.get(bucket)) for bucket in buckets],
            "routes": routes,
        }


def _float(row: dict[str, Any] | None, key: str) -> float | None:
    if not row or row.get(key) is None:
        return None
    return float(row[key])


def _rollup_json(bucket: datetime, row: dict[str, Any] | None) -> dict[str, Any]:
    fields = (
        "request_count", "error_count", "average_latency_ms",
        "p50_latency_ms", "p95_latency_ms", "p99_latency_ms",
        "maximum_latency_ms", "average_cpu_percent", "maximum_cpu_percent",
        "average_memory_bytes", "maximum_memory_bytes", "throttled_usec",
        "oom_kill_count", "restart_count",
    )
    result: dict[str, Any] = {"bucket": bucket.isoformat()}
    for field in fields:
        value = row.get(field) if row else None
        result[field] = float(value) if isinstance(value, (Decimal, float)) else value
    if row and row.get("request_count"):
        result["error_rate_percent"] = (
            row["error_count"] / row["request_count"] * 100
        )
    else:
        result["error_rate_percent"] = None
    return result
