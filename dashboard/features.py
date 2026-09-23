"""Build versioned, five-second model features for labeled experiments."""

from __future__ import annotations

import math
import uuid
from collections import defaultdict
from datetime import UTC, datetime, timedelta
from statistics import fmean
from typing import Any


FEATURE_VERSION = 1
BUCKET_SECONDS = 5
BUILD_GRACE = timedelta(seconds=30)
ACTIVE_STATUSES = ("baseline", "active", "recovering")
BUILDABLE_STATUSES = ("completed", "recovery_failed")

BASE_COLUMNS = (
    "experiment_id", "feature_version", "phase", "bucket_start",
    "phase_bucket_index", "sequence_index", "resource_id", "resource_kind",
)
FEATURE_COLUMNS = (
    "sample_count",
    "cpu_usage_average", "cpu_usage_maximum", "cpu_limit_cores_average",
    "cpu_throttled_usec", "cpu_pressure_average", "cpu_pressure_maximum",
    "memory_current_average_bytes", "memory_current_maximum_bytes",
    "memory_limit_average_bytes", "memory_usage_average_percent",
    "memory_usage_maximum_percent", "memory_high_count", "oom_kill_count",
    "memory_pressure_average", "memory_pressure_maximum",
    "network_rx_average_bytes_per_second", "network_rx_maximum_bytes_per_second",
    "network_tx_average_bytes_per_second", "network_tx_maximum_bytes_per_second",
    "tcp_retransmits", "disk_read_average_bytes_per_second",
    "disk_read_maximum_bytes_per_second", "disk_write_average_bytes_per_second",
    "disk_write_maximum_bytes_per_second", "io_pressure_average",
    "io_pressure_maximum", "restart_count_maximum", "restart_count_delta",
    "ready_ratio", "cpu_request_cores_average", "memory_request_average_bytes",
    "desired_replicas_maximum", "ready_replicas_minimum",
    "unavailable_replicas_maximum", "generation_mismatch", "pod_phase",
    "container_state", "last_termination_reason", "server_request_count",
    "server_error_count", "server_latency_average_ms", "server_latency_p50_ms",
    "server_latency_p95_ms", "server_latency_p99_ms",
    "server_latency_maximum_ms", "client_request_count", "client_error_count",
    "client_latency_average_ms", "client_latency_p95_ms",
    "warning_event_count", "normal_event_count", "event_occurrence_count",
)
ALL_COLUMNS = BASE_COLUMNS + FEATURE_COLUMNS


def _mean(values: list[float]) -> float | None:
    return float(fmean(values)) if values else None


def _integer_mean(values: list[float]) -> int | None:
    return round(fmean(values)) if values else None


def _maximum(values: list[float]) -> float | None:
    return float(max(values)) if values else None


def _integer_maximum(values: list[float]) -> int | None:
    return int(max(values)) if values else None


def _integer_minimum(values: list[float]) -> int | None:
    return int(min(values)) if values else None


def _integer_sum(values: list[float]) -> int | None:
    return int(sum(values)) if values else None


def percentile(values: list[float], quantile: float) -> float | None:
    """Match PostgreSQL percentile_cont interpolation for a small value list."""
    if not values:
        return None
    ordered = sorted(float(value) for value in values)
    position = (len(ordered) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1 - weight) + ordered[upper] * weight


def _number(value: Any) -> float | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _phase_ranges(experiment: dict[str, Any]) -> list[tuple[str, datetime, datetime]]:
    ranges = [
        ("baseline", experiment["baseline_started_at"], experiment["active_started_at"]),
        ("active", experiment["active_started_at"], experiment["active_ended_at"]),
        ("recovery", experiment["active_ended_at"], experiment["recovery_completed_at"]),
    ]
    if any(start is None or end is None or end <= start for _, start, end in ranges):
        raise ValueError("experiment does not have complete, ordered phase timestamps")
    return ranges


def _bucket_for(
    timestamp: datetime,
    ranges: list[tuple[str, datetime, datetime]],
) -> tuple[str, datetime] | None:
    for phase, start, end in ranges:
        if start <= timestamp < end:
            offset = int((timestamp - start).total_seconds())
            return phase, start + timedelta(seconds=(offset // BUCKET_SECONDS) * BUCKET_SECONDS)
    return None


def _empty_feature_row(
    experiment_id: uuid.UUID,
    phase: str,
    bucket_start: datetime,
    phase_bucket_index: int,
    sequence_index: int,
    resource: dict[str, Any],
) -> dict[str, Any]:
    row = {column: None for column in ALL_COLUMNS}
    row.update(
        {
            "experiment_id": experiment_id,
            "feature_version": FEATURE_VERSION,
            "phase": phase,
            "bucket_start": bucket_start,
            "phase_bucket_index": phase_bucket_index,
            "sequence_index": sequence_index,
            "resource_id": int(resource["id"]),
            "resource_kind": int(resource["kind"]),
            "sample_count": 0,
            "server_request_count": 0,
            "server_error_count": 0,
            "client_request_count": 0,
            "client_error_count": 0,
            "warning_event_count": 0,
            "normal_event_count": 0,
            "event_occurrence_count": 0,
        }
    )
    return row


def _eligible_resources(
    connection: Any,
    namespace: str,
    window_start: datetime,
    window_end: datetime,
) -> list[dict[str, Any]]:
    return connection.execute(
        """
        WITH RECURSIVE selected AS (
            SELECT resource.*
            FROM telemetry.resources resource
            WHERE resource.namespace_name = %s
              AND resource.kind BETWEEN 2 AND 4
              AND resource.first_seen_at < %s
              AND resource.last_seen_at >= %s
            UNION
            SELECT parent.*
            FROM telemetry.resources parent
            JOIN selected child ON child.parent_resource_id = parent.id
            WHERE parent.kind BETWEEN 1 AND 4
        )
        SELECT DISTINCT * FROM selected ORDER BY kind, id
        """,
        (namespace, window_end, window_start),
    ).fetchall()


def _query_source_rows(
    connection: Any,
    resources: list[dict[str, Any]],
    namespace: str,
    window_start: datetime,
    window_end: datetime,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    resource_ids = [int(resource["id"]) for resource in resources]
    if not resource_ids:
        return [], [], []
    samples = connection.execute(
        """
        SELECT * FROM telemetry.infra_samples
        WHERE resource_id = ANY(%s)
          AND sampled_at >= %s AND sampled_at < %s
        ORDER BY sampled_at, resource_id
        """,
        (resource_ids, window_start, window_end),
    ).fetchall()
    spans = connection.execute(
        """
        SELECT span.started_at, span.span_kind, span.duration_us,
               span.status_code, span.http_status_code, span.error_type,
               resource.deployment_name
        FROM telemetry.spans span
        JOIN telemetry.resources resource ON resource.id = span.resource_id
        WHERE resource.namespace_name = %s
          AND span.started_at >= %s AND span.started_at < %s
          AND span.span_kind IN (2, 3)
        ORDER BY span.started_at
        """,
        (namespace, window_start, window_end),
    ).fetchall()
    events = connection.execute(
        """
        SELECT resource_id, last_seen_at, severity, occurrence_count
        FROM telemetry.events
        WHERE resource_id = ANY(%s)
          AND last_seen_at >= %s AND last_seen_at < %s
        ORDER BY last_seen_at
        """,
        (resource_ids, window_start, window_end),
    ).fetchall()
    return samples, spans, events


def build_feature_rows(
    experiment: dict[str, Any],
    resources: list[dict[str, Any]],
    samples: list[dict[str, Any]],
    spans: list[dict[str, Any]],
    events: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Create a complete resource-by-time grid and aggregate source records."""
    ranges = _phase_ranges(experiment)
    rows: dict[tuple[str, datetime, int], dict[str, Any]] = {}
    sequence_index = 0
    for phase, phase_start, phase_end in ranges:
        bucket_start = phase_start
        phase_index = 0
        while bucket_start < phase_end:
            bucket_end = min(bucket_start + timedelta(seconds=BUCKET_SECONDS), phase_end)
            for resource in resources:
                if resource["first_seen_at"] >= bucket_end:
                    continue
                if resource["last_seen_at"] < bucket_start:
                    continue
                key = (phase, bucket_start, int(resource["id"]))
                rows[key] = _empty_feature_row(
                    experiment["id"], phase, bucket_start, phase_index,
                    sequence_index, resource,
                )
            bucket_start += timedelta(seconds=BUCKET_SECONDS)
            phase_index += 1
            sequence_index += 1

    sample_groups: dict[tuple[str, datetime, int], list[dict[str, Any]]] = defaultdict(list)
    for sample in samples:
        location = _bucket_for(sample["sampled_at"], ranges)
        if location:
            key = (*location, int(sample["resource_id"]))
            if key in rows:
                sample_groups[key].append(sample)

    numeric_rules: dict[str, tuple[str, str]] = {
        "cpu_usage_percent": ("cpu_usage_average", "average"),
        "cpu_limit_cores": ("cpu_limit_cores_average", "average"),
        "cpu_throttled_usec_delta": ("cpu_throttled_usec", "sum"),
        "cpu_pressure_avg10": ("cpu_pressure_average", "average"),
        "memory_current_bytes": ("memory_current_average_bytes", "integer_average"),
        "memory_limit_bytes": ("memory_limit_average_bytes", "integer_average"),
        "memory_usage_percent": ("memory_usage_average_percent", "average"),
        "memory_high_delta": ("memory_high_count", "sum"),
        "oom_kill_delta": ("oom_kill_count", "sum"),
        "memory_pressure_full_avg10": ("memory_pressure_average", "average"),
        "network_rx_bytes_per_second": ("network_rx_average_bytes_per_second", "integer_average"),
        "network_tx_bytes_per_second": ("network_tx_average_bytes_per_second", "integer_average"),
        "tcp_retransmits_delta": ("tcp_retransmits", "sum"),
        "disk_read_bytes_per_second": ("disk_read_average_bytes_per_second", "integer_average"),
        "disk_write_bytes_per_second": ("disk_write_average_bytes_per_second", "integer_average"),
        "io_pressure_full_avg10": ("io_pressure_average", "average"),
    }
    operators = {
        "average": _mean,
        "integer_average": _integer_mean,
        "sum": _integer_sum,
    }
    for key, group in sample_groups.items():
        target = rows[key]
        target["sample_count"] = len(group)
        for source_name, (target_name, operation) in numeric_rules.items():
            values = [_number(item.get(source_name)) for item in group]
            target[target_name] = operators[operation]([value for value in values if value is not None])

        maximum_rules = {
            "cpu_usage_percent": ("cpu_usage_maximum", _maximum),
            "cpu_pressure_avg10": ("cpu_pressure_maximum", _maximum),
            "memory_current_bytes": ("memory_current_maximum_bytes", _integer_maximum),
            "memory_usage_percent": ("memory_usage_maximum_percent", _maximum),
            "memory_pressure_full_avg10": ("memory_pressure_maximum", _maximum),
            "network_rx_bytes_per_second": ("network_rx_maximum_bytes_per_second", _integer_maximum),
            "network_tx_bytes_per_second": ("network_tx_maximum_bytes_per_second", _integer_maximum),
            "disk_read_bytes_per_second": ("disk_read_maximum_bytes_per_second", _integer_maximum),
            "disk_write_bytes_per_second": ("disk_write_maximum_bytes_per_second", _integer_maximum),
            "io_pressure_full_avg10": ("io_pressure_maximum", _maximum),
        }
        for source_name, (target_name, operation) in maximum_rules.items():
            values = [_number(item.get(source_name)) for item in group]
            target[target_name] = operation([value for value in values if value is not None])

        restart_values = [_number(item.get("restart_count")) for item in group]
        restarts = [value for value in restart_values if value is not None]
        if restarts:
            target["restart_count_maximum"] = int(max(restarts))
            target["restart_count_delta"] = int(max(restarts) - min(restarts))
        ready_values = [item.get("ready") for item in group if item.get("ready") is not None]
        if ready_values:
            target["ready_ratio"] = sum(bool(value) for value in ready_values) / len(ready_values)

        extras = [item.get("extra_metrics") or {} for item in group]
        for source_name, target_name, operation in (
            ("cpu_request_cores", "cpu_request_cores_average", _mean),
            ("memory_request_bytes", "memory_request_average_bytes", _integer_mean),
            ("desired_replicas", "desired_replicas_maximum", _integer_maximum),
            ("ready_replicas", "ready_replicas_minimum", _integer_minimum),
            ("unavailable_replicas", "unavailable_replicas_maximum", _integer_maximum),
        ):
            values = [_number(extra.get(source_name)) for extra in extras]
            target[target_name] = operation([value for value in values if value is not None])
        mismatches = []
        for extra in extras:
            generation = _number(extra.get("generation"))
            observed = _number(extra.get("observed_generation"))
            if generation is not None and observed is not None:
                mismatches.append(generation != observed)
        target["generation_mismatch"] = any(mismatches) if mismatches else None
        for source_name, target_name in (
            ("pod_phase", "pod_phase"),
            ("container_state", "container_state"),
            ("last_termination_reason", "last_termination_reason"),
        ):
            for sample in reversed(group):
                value = (sample.get("extra_metrics") or {}).get(source_name)
                if value not in (None, ""):
                    target[target_name] = str(value)
                    break

    deployment_resources = {
        resource["deployment_name"]: int(resource["id"])
        for resource in resources
        if int(resource["kind"]) == 2 and resource.get("deployment_name")
    }
    span_groups: dict[tuple[str, datetime, int, int], list[dict[str, Any]]] = defaultdict(list)
    for span in spans:
        deployment_id = deployment_resources.get(span.get("deployment_name"))
        location = _bucket_for(span["started_at"], ranges)
        if deployment_id and location:
            key = (*location, deployment_id, int(span["span_kind"]))
            span_groups[key].append(span)
    for (phase, bucket_start, resource_id, span_kind), group in span_groups.items():
        target = rows.get((phase, bucket_start, resource_id))
        if target is None:
            continue
        durations = [float(item["duration_us"]) / 1000.0 for item in group]
        errors = sum(
            int(item["status_code"] == 2 or (item.get("http_status_code") or 0) >= 500)
            for item in group
        )
        prefix = "server" if span_kind == 2 else "client"
        target[f"{prefix}_request_count"] = len(group)
        target[f"{prefix}_error_count"] = errors
        target[f"{prefix}_latency_average_ms"] = _mean(durations)
        target[f"{prefix}_latency_p95_ms"] = percentile(durations, 0.95)
        if prefix == "server":
            target["server_latency_p50_ms"] = percentile(durations, 0.50)
            target["server_latency_p99_ms"] = percentile(durations, 0.99)
            target["server_latency_maximum_ms"] = _maximum(durations)

    for event in events:
        location = _bucket_for(event["last_seen_at"], ranges)
        if not location:
            continue
        target = rows.get((*location, int(event["resource_id"])))
        if target is None:
            continue
        if int(event["severity"]) >= 2:
            target["warning_event_count"] += 1
        else:
            target["normal_event_count"] += 1
        target["event_occurrence_count"] += int(event["occurrence_count"])

    return sorted(
        rows.values(),
        key=lambda row: (row["sequence_index"], row["resource_kind"], row["resource_id"]),
    )


def build_experiment_features(
    database: Any,
    experiment_id: uuid.UUID,
    *,
    force: bool = False,
) -> int | None:
    """Build one experiment atomically; return None when another builder owns it."""
    lock_id = int(experiment_id.int & ((1 << 63) - 1))
    try:
        with database.pool.connection() as connection:
            with connection.transaction():
                acquired = connection.execute(
                    "SELECT pg_try_advisory_xact_lock(%s) AS acquired", (lock_id,)
                ).fetchone()["acquired"]
                if not acquired:
                    return None
                experiment = connection.execute(
                    "SELECT * FROM telemetry.fault_experiments WHERE id = %s FOR UPDATE",
                    (experiment_id,),
                ).fetchone()
                if not experiment:
                    raise ValueError("experiment was not found")
                if experiment["status"] not in BUILDABLE_STATUSES:
                    raise ValueError("experiment is not in a buildable terminal state")
                _phase_ranges(experiment)
                previous = connection.execute(
                    """
                    SELECT * FROM telemetry.experiment_feature_builds
                    WHERE experiment_id = %s AND feature_version = %s
                    """,
                    (experiment_id, FEATURE_VERSION),
                ).fetchone()
                if previous and previous["status"] == "completed" and not force:
                    return int(previous["row_count"])
                connection.execute(
                    """
                    INSERT INTO telemetry.experiment_feature_builds (
                        experiment_id, feature_version, status, started_at,
                        attempt_count, row_count
                    ) VALUES (%s, %s, 'building', now(), 1, 0)
                    ON CONFLICT (experiment_id, feature_version) DO UPDATE SET
                        status = 'building', started_at = now(), completed_at = NULL,
                        attempt_count = experiment_feature_builds.attempt_count + 1,
                        row_count = 0, error_message = NULL
                    """,
                    (experiment_id, FEATURE_VERSION),
                )
                connection.execute(
                    """
                    DELETE FROM telemetry.experiment_feature_buckets
                    WHERE experiment_id = %s AND feature_version = %s
                    """,
                    (experiment_id, FEATURE_VERSION),
                )
                resources = _eligible_resources(
                    connection,
                    experiment["namespace_name"],
                    experiment["baseline_started_at"],
                    experiment["recovery_completed_at"],
                )
                if not resources:
                    raise ValueError("experiment has no eligible telemetry resources")
                samples, spans, events = _query_source_rows(
                    connection,
                    resources,
                    experiment["namespace_name"],
                    experiment["baseline_started_at"],
                    experiment["recovery_completed_at"],
                )
                rows = build_feature_rows(experiment, resources, samples, spans, events)
                if not rows:
                    raise ValueError("experiment produced no feature buckets")
                placeholders = ", ".join(f"%({column})s" for column in ALL_COLUMNS)
                with connection.cursor() as cursor:
                    cursor.executemany(
                        f"""
                        INSERT INTO telemetry.experiment_feature_buckets (
                            {', '.join(ALL_COLUMNS)}
                        ) VALUES ({placeholders})
                        """,
                        rows,
                    )
                connection.execute(
                    """
                    UPDATE telemetry.experiment_feature_builds
                    SET status = 'completed', completed_at = now(),
                        row_count = %s, error_message = NULL
                    WHERE experiment_id = %s AND feature_version = %s
                    """,
                    (len(rows), experiment_id, FEATURE_VERSION),
                )
                return len(rows)
    except Exception as error:
        with database.pool.connection() as connection:
            connection.execute(
                """
                INSERT INTO telemetry.experiment_feature_builds (
                    experiment_id, feature_version, status, started_at,
                    completed_at, attempt_count, row_count, error_message
                )
                SELECT id, %s, 'failed', now(), now(), 1, 0, %s
                FROM telemetry.fault_experiments WHERE id = %s
                ON CONFLICT (experiment_id, feature_version) DO UPDATE SET
                    status = 'failed', completed_at = now(),
                    attempt_count = experiment_feature_builds.attempt_count + 1,
                    row_count = 0, error_message = EXCLUDED.error_message
                """,
                (FEATURE_VERSION, str(error)[:4000], experiment_id),
            )
        raise


def build_pending_features(
    database: Any,
    *,
    now: datetime | None = None,
    limit: int = 20,
) -> dict[str, int]:
    current = now or datetime.now(UTC)
    cutoff = current - BUILD_GRACE
    with database.pool.connection() as connection:
        with connection.transaction():
            expired = connection.execute(
                """
                UPDATE telemetry.fault_experiments
                SET status = 'failed', failed_at = COALESCE(failed_at, %s),
                    error_message = COALESCE(
                        error_message, 'experiment expired before completion'
                    ), updated_at = now()
                WHERE status = ANY(%s) AND expires_at < %s
                """,
                (current, list(ACTIVE_STATUSES), current),
            ).rowcount
            candidates = connection.execute(
                """
                SELECT experiment.id
                FROM telemetry.fault_experiments experiment
                LEFT JOIN telemetry.experiment_feature_builds build
                  ON build.experiment_id = experiment.id
                 AND build.feature_version = %s
                WHERE experiment.status = ANY(%s)
                  AND experiment.recovery_completed_at <= %s
                  AND (build.status IS NULL OR build.status = 'failed')
                ORDER BY experiment.recovery_completed_at
                LIMIT %s
                """,
                (FEATURE_VERSION, list(BUILDABLE_STATUSES), cutoff, limit),
            ).fetchall()
    completed = 0
    failed = 0
    for candidate in candidates:
        try:
            result = build_experiment_features(database, candidate["id"])
            if result is not None:
                completed += 1
        except Exception:
            failed += 1
    return {
        "expired": int(expired),
        "completed": completed,
        "failed": failed,
    }
