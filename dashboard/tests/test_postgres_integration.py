"""Opt-in tests against a dedicated PostgreSQL database.

Set TEST_DATABASE_URL to run these tests. Test records use a random identity so
the suite does not drop the schema or alter unrelated telemetry.
"""

from __future__ import annotations

import os
import sys
import unittest
import uuid
from datetime import UTC, datetime, timedelta
from pathlib import Path

from opentelemetry.proto.collector.trace.v1.trace_service_pb2 import (
    ExportTraceServiceRequest,
)


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from database import TelemetryDatabase, resource_uuid  # noqa: E402
from maintenance import enforce_retention, rollup_bucket, rollup_recent  # noqa: E402


def add_string_attribute(target, key: str, value: str) -> None:
    attribute = target.attributes.add()
    attribute.key = key
    attribute.value.string_value = value


def add_integer_attribute(target, key: str, value: int) -> None:
    attribute = target.attributes.add()
    attribute.key = key
    attribute.value.int_value = value


class PostgreSQLIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        database_url = os.environ.get("TEST_DATABASE_URL")
        if not database_url:
            raise unittest.SkipTest("TEST_DATABASE_URL is not configured")
        cls.database = TelemetryDatabase(
            database_url,
            Path(__file__).resolve().parents[1] / "migrations",
        )
        cls.database.open(timeout=30)

    @classmethod
    def tearDownClass(cls) -> None:
        if hasattr(cls, "database"):
            cls.database.close()

    def test_idempotent_ingestion_resource_enrichment_and_rollup(self) -> None:
        suffix = uuid.uuid4().hex[:12]
        service = f"payment-it-{suffix}"
        pod_uid = f"pod-it-{suffix}"
        container_id = suffix * 5 + suffix[:4]
        bucket = datetime.now(UTC).replace(second=0, microsecond=0) - timedelta(
            minutes=1
        )

        self.database.run_migrations()
        self.database.run_migrations()

        trace_request = ExportTraceServiceRequest()
        resource_spans = trace_request.resource_spans.add()
        add_string_attribute(resource_spans.resource, "service.name", service)
        add_string_attribute(
            resource_spans.resource, "k8s.namespace.name", "infrastructure-demo"
        )
        add_string_attribute(resource_spans.resource, "service.instance.id", pod_uid)
        add_string_attribute(resource_spans.resource, "k8s.pod.name", f"{service}-abcde")
        add_string_attribute(resource_spans.resource, "k8s.container.name", service)
        scope_spans = resource_spans.scope_spans.add()
        for index, (duration_ms, status_code) in enumerate(
            ((100, 200), (200, 200), (300, 500))
        ):
            span = scope_spans.spans.add()
            span.trace_id = uuid.uuid4().bytes
            span.span_id = (index + 1).to_bytes(8, "big")
            span.name = "POST /pay"
            span.kind = 2
            span.start_time_unix_nano = int(
                (bucket + timedelta(seconds=10 + index)).timestamp() * 1_000_000_000
            )
            span.end_time_unix_nano = (
                span.start_time_unix_nano + duration_ms * 1_000_000
            )
            add_string_attribute(span, "http.route", "/pay")
            add_integer_attribute(span, "http.response.status_code", status_code)
            if status_code == 500:
                span.status.code = 2

        client_span = scope_spans.spans.add()
        client_span.trace_id = uuid.uuid4().bytes
        client_span.span_id = (9).to_bytes(8, "big")
        client_span.name = "POST payment"
        client_span.kind = 3
        client_span.start_time_unix_nano = int(
            (bucket + timedelta(seconds=20)).timestamp() * 1_000_000_000
        )
        client_span.end_time_unix_nano = (
            client_span.start_time_unix_nano + 900 * 1_000_000
        )

        payload = trace_request.SerializeToString()
        self.database.ingest_otlp_traces(payload)
        self.database.ingest_otlp_traces(payload)

        sampled_at = bucket + timedelta(seconds=30)
        snapshot = {
            "node": {
                "timestamp_unix_ms": int(sampled_at.timestamp() * 1000),
                "hostname": f"node-{suffix}",
                "logical_cpu_count": 4,
                "cpu_usage_percent": 20.0,
                "memory_usage_percent": 40.0,
                "memory_total_kb": 1_000_000,
                "memory_available_kb": 600_000,
            },
            "deployments": [
                {
                    "namespace": "infrastructure-demo",
                    "deployment_name": service,
                    "deployment_uid": f"deployment-{suffix}",
                    "cpu_usage_percent": 75.0,
                    "memory_current_bytes": 80_000_000,
                    "throttled_usec_delta": 1200,
                    "oom_kill_delta": 0,
                    "restart_count": 1,
                }
            ],
            "pods": [
                {
                    "namespace": "infrastructure-demo",
                    "pod_uid": pod_uid,
                    "pod_name": f"{service}-abcde",
                    "node_name": f"node-{suffix}",
                    "workload_kind": "Deployment",
                    "workload_name": service,
                    "cpu_usage_percent": 75.0,
                    "memory_current_bytes": 80_000_000,
                    "all_containers_ready": True,
                    "restart_count": 1,
                }
            ],
            "containers": [
                {
                    "namespace": "infrastructure-demo",
                    "pod_uid": pod_uid,
                    "pod_name": f"{service}-abcde",
                    "node_name": f"node-{suffix}",
                    "container_id": container_id,
                    "container_name": service,
                    "workload_kind": "Deployment",
                    "workload_name": service,
                    "cpu_usage_percent": 75.0,
                    "memory_current_bytes": 80_000_000,
                    "container_ready": True,
                    "restart_count": 1,
                }
            ],
            "top_cpu_processes": [],
            "top_memory_processes": [],
            "kubernetes_events": [
                {
                    "namespace": "infrastructure-demo",
                    "event_type": "Warning",
                    "reason": "IntegrationTest",
                    "object_uid": pod_uid,
                    "message": f"integration event {suffix}",
                    "count": 3,
                }
            ],
        }
        self.database.ingest_snapshot(snapshot)
        self.database.ingest_snapshot(snapshot)

        rollup_bucket(self.database, bucket)
        with self.database.pool.connection() as connection:
            container = connection.execute(
                """
                SELECT count(*) AS count, max(container_id) AS container_id,
                       max(parent_resource_id) AS parent_resource_id
                FROM telemetry.resources
                WHERE kind = 4 AND pod_uid = %s AND container_name = %s
                """,
                (pod_uid, service),
            ).fetchone()
            span_count = connection.execute(
                """
                SELECT count(*) AS count FROM telemetry.spans span
                JOIN telemetry.resources resource ON resource.id = span.resource_id
                WHERE resource.pod_uid = %s AND resource.container_name = %s
                """,
                (pod_uid, service),
            ).fetchone()["count"]
            aggregate = connection.execute(
                """
                SELECT * FROM telemetry.service_rollups_1m
                WHERE bucket = %s AND namespace_name = 'infrastructure-demo'
                  AND deployment_name = %s AND http_route = ''
                """,
                (bucket, service),
            ).fetchone()
            route = connection.execute(
                """
                SELECT * FROM telemetry.service_rollups_1m
                WHERE bucket = %s AND namespace_name = 'infrastructure-demo'
                  AND deployment_name = %s AND http_route = '/pay'
                """,
                (bucket, service),
            ).fetchone()

        self.assertEqual(container["count"], 1)
        self.assertEqual(container["container_id"], container_id)
        self.assertIsNotNone(container["parent_resource_id"])
        self.assertEqual(span_count, 4)
        self.assertEqual(aggregate["request_count"], 3)
        self.assertEqual(aggregate["error_count"], 1)
        self.assertAlmostEqual(float(aggregate["p50_latency_ms"]), 200.0)
        self.assertAlmostEqual(float(aggregate["p95_latency_ms"]), 290.0)
        self.assertAlmostEqual(float(aggregate["p99_latency_ms"]), 298.0)
        self.assertAlmostEqual(float(aggregate["average_cpu_percent"]), 75.0)
        self.assertEqual(route["request_count"], 3)
        self.assertIsNone(route["average_cpu_percent"])

        overview = self.database.query_overview(60)
        deployment = self.database.query_deployment(
            "infrastructure-demo", service, 60
        )
        self.assertEqual(len(overview["node"]), 60)
        self.assertEqual(len(overview["deployments"][0]["series"]), 60)
        self.assertIsNotNone(deployment)
        self.assertEqual(len(deployment["series"]), 60)
        self.assertEqual(deployment["routes"][0]["http_route"], "/pay")

        late_span = scope_spans.spans.add()
        late_span.trace_id = uuid.uuid4().bytes
        late_span.span_id = (10).to_bytes(8, "big")
        late_span.name = "POST /pay"
        late_span.kind = 2
        late_span.start_time_unix_nano = int(
            (bucket + timedelta(seconds=45)).timestamp() * 1_000_000_000
        )
        late_span.end_time_unix_nano = late_span.start_time_unix_nano + 400_000_000
        add_string_attribute(late_span, "http.route", "/pay")
        add_integer_attribute(late_span, "http.response.status_code", 200)
        self.database.ingest_otlp_traces(trace_request.SerializeToString())
        rollup_recent(self.database)
        with self.database.pool.connection() as connection:
            updated_count = connection.execute(
                """
                SELECT request_count FROM telemetry.service_rollups_1m
                WHERE bucket = %s AND namespace_name = 'infrastructure-demo'
                  AND deployment_name = %s AND http_route = ''
                """,
                (bucket, service),
            ).fetchone()["request_count"]
        self.assertEqual(updated_count, 4)

    def test_retention_waits_for_a_rollup_before_dropping_old_raw_data(self) -> None:
        old_day = datetime(2001, 1, 2, tzinfo=UTC)
        suffix = uuid.uuid4().hex[:12]
        self.database.ensure_partition("spans", old_day.date())
        self.database.ensure_partition("infra_samples", old_day.date())

        with self.database.pool.connection() as connection:
            with connection.transaction():
                resource_id = connection.execute(
                    """
                    INSERT INTO telemetry.resources (
                        resource_key, kind, service_name, namespace_name,
                        deployment_name, first_seen_at, last_seen_at
                    ) VALUES (%s, 2, %s, 'retention-test', %s, %s, %s)
                    RETURNING id
                    """,
                    (
                        resource_uuid(2, "retention-test", suffix),
                        suffix,
                        suffix,
                        old_day,
                        old_day,
                    ),
                ).fetchone()["id"]
                connection.execute(
                    """
                    INSERT INTO telemetry.spans (
                        started_at, trace_id, span_id, resource_id,
                        operation_name, span_kind, duration_us, status_code
                    ) VALUES (%s, %s, %s, %s, 'old request', 2, 1000, 0)
                    """,
                    (old_day, uuid.uuid4().bytes, (1).to_bytes(8, "big"), resource_id),
                )
                connection.execute(
                    """
                    INSERT INTO telemetry.infra_samples (
                        sampled_at, resource_id, cpu_usage_percent
                    ) VALUES (%s, %s, 10)
                    """,
                    (old_day, resource_id),
                )

        first_removed = enforce_retention(self.database)
        self.assertNotIn("spans_20010102", first_removed)
        self.assertNotIn("infra_samples_20010102", first_removed)

        with self.database.pool.connection() as connection:
            connection.execute(
                """
                INSERT INTO telemetry.service_rollups_1m (
                    bucket, namespace_name, deployment_name,
                    service_name, http_route, request_count, error_count
                ) VALUES (%s, 'retention-test', %s, %s, '', 1, 0)
                """,
                (old_day, suffix, suffix),
            )
        second_removed = enforce_retention(self.database)
        self.assertIn("spans_20010102", second_removed)
        self.assertIn("infra_samples_20010102", second_removed)

        with self.database.pool.connection() as connection:
            remaining = connection.execute(
                """
                SELECT to_regclass('telemetry.spans_20010102') AS spans,
                       to_regclass('telemetry.infra_samples_20010102') AS samples,
                       to_regclass(%s) AS current_spans
                """,
                (f"telemetry.spans_{datetime.now(UTC):%Y%m%d}",),
            ).fetchone()
        self.assertIsNone(remaining["spans"])
        self.assertIsNone(remaining["samples"])
        self.assertIsNotNone(remaining["current_spans"])


if __name__ == "__main__":
    unittest.main()
