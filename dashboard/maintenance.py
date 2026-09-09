"""Build one-minute rollups and enforce telemetry retention."""

from __future__ import annotations

import argparse
import os
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

from psycopg import sql

from database import TelemetryDatabase


MIGRATIONS_ROOT = Path(__file__).resolve().parent / "migrations"


ROLLUP_COLUMNS = (
    "bucket", "namespace_name", "deployment_name", "service_name",
    "http_route", "request_count", "error_count", "average_latency_ms",
    "p50_latency_ms", "p95_latency_ms", "p99_latency_ms",
    "maximum_latency_ms", "average_cpu_percent", "maximum_cpu_percent",
    "average_memory_bytes", "maximum_memory_bytes", "throttled_usec",
    "oom_kill_count", "restart_count",
)


def rollup_recent(database: TelemetryDatabase, bucket_count: int = 5) -> int:
    end = datetime.now(UTC).replace(second=0, microsecond=0)
    written = 0
    for offset in range(bucket_count, 0, -1):
        written += rollup_bucket(database, end - timedelta(minutes=offset))
    return written


def rollup_bucket(database: TelemetryDatabase, bucket: datetime) -> int:
    bucket_end = bucket + timedelta(minutes=1)
    with database.pool.connection() as connection:
        with connection.transaction():
            connection.execute(
                "DELETE FROM telemetry.service_rollups_1m WHERE bucket = %s",
                (bucket,),
            )
            aggregate_rows = connection.execute(
                """
                SELECT
                    COALESCE(NULLIF(resource.namespace_name, ''), 'unknown') AS namespace_name,
                    COALESCE(NULLIF(resource.deployment_name, ''), resource.service_name, 'unknown') AS deployment_name,
                    COALESCE(NULLIF(resource.service_name, ''), resource.deployment_name, 'unknown') AS service_name,
                    count(*) AS request_count,
                    count(*) FILTER (
                        WHERE span.status_code = 2 OR span.http_status_code >= 500
                    ) AS error_count,
                    avg(span.duration_us) / 1000.0 AS average_latency_ms,
                    percentile_cont(0.50) WITHIN GROUP (ORDER BY span.duration_us) / 1000.0 AS p50_latency_ms,
                    percentile_cont(0.95) WITHIN GROUP (ORDER BY span.duration_us) / 1000.0 AS p95_latency_ms,
                    percentile_cont(0.99) WITHIN GROUP (ORDER BY span.duration_us) / 1000.0 AS p99_latency_ms,
                    max(span.duration_us) / 1000.0 AS maximum_latency_ms
                FROM telemetry.spans span
                JOIN telemetry.resources resource ON resource.id = span.resource_id
                WHERE span.started_at >= %s AND span.started_at < %s
                  AND span.span_kind = 2
                GROUP BY 1, 2, 3
                """,
                (bucket, bucket_end),
            ).fetchall()
            route_rows = connection.execute(
                """
                SELECT
                    COALESCE(NULLIF(resource.namespace_name, ''), 'unknown') AS namespace_name,
                    COALESCE(NULLIF(resource.deployment_name, ''), resource.service_name, 'unknown') AS deployment_name,
                    COALESCE(NULLIF(resource.service_name, ''), resource.deployment_name, 'unknown') AS service_name,
                    span.http_route,
                    count(*) AS request_count,
                    count(*) FILTER (
                        WHERE span.status_code = 2 OR span.http_status_code >= 500
                    ) AS error_count,
                    avg(span.duration_us) / 1000.0 AS average_latency_ms,
                    percentile_cont(0.50) WITHIN GROUP (ORDER BY span.duration_us) / 1000.0 AS p50_latency_ms,
                    percentile_cont(0.95) WITHIN GROUP (ORDER BY span.duration_us) / 1000.0 AS p95_latency_ms,
                    percentile_cont(0.99) WITHIN GROUP (ORDER BY span.duration_us) / 1000.0 AS p99_latency_ms,
                    max(span.duration_us) / 1000.0 AS maximum_latency_ms
                FROM telemetry.spans span
                JOIN telemetry.resources resource ON resource.id = span.resource_id
                WHERE span.started_at >= %s AND span.started_at < %s
                  AND span.span_kind = 2
                  AND COALESCE(span.http_route, '') <> ''
                GROUP BY 1, 2, 3, 4
                """,
                (bucket, bucket_end),
            ).fetchall()
            infrastructure_rows = connection.execute(
                """
                SELECT
                    COALESCE(NULLIF(resource.namespace_name, ''), 'unknown') AS namespace_name,
                    COALESCE(NULLIF(resource.deployment_name, ''), resource.service_name, 'unknown') AS deployment_name,
                    COALESCE(NULLIF(resource.service_name, ''), resource.deployment_name, 'unknown') AS service_name,
                    avg(sample.cpu_usage_percent) AS average_cpu_percent,
                    max(sample.cpu_usage_percent) AS maximum_cpu_percent,
                    avg(sample.memory_current_bytes)::bigint AS average_memory_bytes,
                    max(sample.memory_current_bytes) AS maximum_memory_bytes,
                    sum(sample.cpu_throttled_usec_delta)::bigint AS throttled_usec,
                    sum(sample.oom_kill_delta)::integer AS oom_kill_count,
                    max(sample.restart_count)::integer AS restart_count
                FROM telemetry.infra_samples sample
                JOIN telemetry.resources resource ON resource.id = sample.resource_id
                WHERE sample.sampled_at >= %s AND sample.sampled_at < %s
                  AND resource.kind = 2
                GROUP BY 1, 2, 3
                """,
                (bucket, bucket_end),
            ).fetchall()

            rows: dict[tuple[str, str, str, str], dict[str, Any]] = {}
            for source in aggregate_rows:
                key = (
                    source["namespace_name"], source["deployment_name"],
                    source["service_name"], "",
                )
                rows[key] = _base_rollup(bucket, source, "")
            for source in infrastructure_rows:
                key = (
                    source["namespace_name"], source["deployment_name"],
                    source["service_name"], "",
                )
                target = rows.setdefault(key, _base_rollup(bucket, source, ""))
                for name in (
                    "average_cpu_percent", "maximum_cpu_percent",
                    "average_memory_bytes", "maximum_memory_bytes",
                    "throttled_usec", "oom_kill_count", "restart_count",
                ):
                    target[name] = source[name]
            for source in route_rows:
                key = (
                    source["namespace_name"], source["deployment_name"],
                    source["service_name"], source["http_route"],
                )
                rows[key] = _base_rollup(bucket, source, source["http_route"])

            if rows:
                with connection.cursor() as cursor:
                    cursor.executemany(
                        f"""
                        INSERT INTO telemetry.service_rollups_1m ({', '.join(ROLLUP_COLUMNS)})
                        VALUES ({', '.join('%(' + name + ')s' for name in ROLLUP_COLUMNS)})
                        ON CONFLICT (
                            bucket, namespace_name, deployment_name, service_name, http_route
                        ) DO UPDATE SET
                            request_count = EXCLUDED.request_count,
                            error_count = EXCLUDED.error_count,
                            average_latency_ms = EXCLUDED.average_latency_ms,
                            p50_latency_ms = EXCLUDED.p50_latency_ms,
                            p95_latency_ms = EXCLUDED.p95_latency_ms,
                            p99_latency_ms = EXCLUDED.p99_latency_ms,
                            maximum_latency_ms = EXCLUDED.maximum_latency_ms,
                            average_cpu_percent = EXCLUDED.average_cpu_percent,
                            maximum_cpu_percent = EXCLUDED.maximum_cpu_percent,
                            average_memory_bytes = EXCLUDED.average_memory_bytes,
                            maximum_memory_bytes = EXCLUDED.maximum_memory_bytes,
                            throttled_usec = EXCLUDED.throttled_usec,
                            oom_kill_count = EXCLUDED.oom_kill_count,
                            restart_count = EXCLUDED.restart_count
                        """,
                        list(rows.values()),
                    )
            return len(rows)


def _base_rollup(
    bucket: datetime, source: dict[str, Any], route: str
) -> dict[str, Any]:
    row = {name: None for name in ROLLUP_COLUMNS}
    row.update(
        {
            "bucket": bucket,
            "namespace_name": source["namespace_name"],
            "deployment_name": source["deployment_name"],
            "service_name": source["service_name"],
            "http_route": route,
            "request_count": source.get("request_count", 0),
            "error_count": source.get("error_count", 0),
        }
    )
    for name in (
        "average_latency_ms", "p50_latency_ms", "p95_latency_ms",
        "p99_latency_ms", "maximum_latency_ms", "average_cpu_percent",
        "maximum_cpu_percent", "average_memory_bytes", "maximum_memory_bytes",
        "throttled_usec", "oom_kill_count", "restart_count",
    ):
        if name in source:
            row[name] = source[name]
    return row


def enforce_retention(
    database: TelemetryDatabase, raw_days: int = 7, summary_days: int = 30
) -> list[str]:
    today = datetime.now(UTC).date()
    database.ensure_partition("spans", today + timedelta(days=1))
    database.ensure_partition("infra_samples", today + timedelta(days=1))
    cutoff = today - timedelta(days=raw_days)
    removed: list[str] = []

    with database.pool.connection() as connection:
        partitions = connection.execute(
            """
            SELECT parent.relname AS parent_name, child.relname AS partition_name
            FROM pg_inherits
            JOIN pg_class parent ON pg_inherits.inhparent = parent.oid
            JOIN pg_class child ON pg_inherits.inhrelid = child.oid
            JOIN pg_namespace namespace ON namespace.oid = child.relnamespace
            WHERE namespace.nspname = 'telemetry'
              AND parent.relname IN ('spans', 'infra_samples')
            """
        ).fetchall()
        for partition in partitions:
            suffix = partition["partition_name"].rsplit("_", 1)[-1]
            try:
                partition_day = datetime.strptime(suffix, "%Y%m%d").date()
            except ValueError:
                continue
            if partition_day >= cutoff:
                continue
            with connection.transaction():
                row_count = connection.execute(
                    sql.SQL("SELECT count(*) AS count FROM telemetry.{}").format(
                        sql.Identifier(partition["partition_name"])
                    )
                ).fetchone()["count"]
                has_rollup = connection.execute(
                    """
                    SELECT EXISTS (
                        SELECT 1 FROM telemetry.service_rollups_1m
                        WHERE bucket >= %s AND bucket < %s
                    ) AS found
                    """,
                    (partition_day, partition_day + timedelta(days=1)),
                ).fetchone()["found"]
                if row_count and not has_rollup:
                    continue
                connection.execute(
                    sql.SQL("DROP TABLE telemetry.{}").format(
                        sql.Identifier(partition["partition_name"])
                    )
                )
                removed.append(partition["partition_name"])
        with connection.transaction():
            summary_cutoff = datetime.now(UTC) - timedelta(days=summary_days)
            connection.execute(
                "DELETE FROM telemetry.service_rollups_1m WHERE bucket < %s",
                (summary_cutoff,),
            )
            connection.execute(
                "DELETE FROM telemetry.events WHERE last_seen_at < %s",
                (summary_cutoff,),
            )
    return removed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("rollup", "retention"))
    args = parser.parse_args()
    database_url = os.environ.get("DATABASE_URL")
    if not database_url:
        raise SystemExit("DATABASE_URL is required")
    database = TelemetryDatabase(database_url, MIGRATIONS_ROOT)
    database.open(timeout=30)
    try:
        if args.command == "rollup":
            print(f"wrote {rollup_recent(database)} rollup rows")
        else:
            removed = enforce_retention(database)
            print(f"removed {len(removed)} expired partitions")
    finally:
        database.close()


if __name__ == "__main__":
    main()
