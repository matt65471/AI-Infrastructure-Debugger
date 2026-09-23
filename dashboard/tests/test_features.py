import sys
import unittest
import uuid
from datetime import UTC, datetime, timedelta
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from features import build_feature_rows, percentile  # noqa: E402


class FeatureAggregationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.started = datetime(2026, 9, 22, 12, 0, tzinfo=UTC)
        self.experiment = {
            "id": uuid.uuid4(),
            "baseline_started_at": self.started,
            "active_started_at": self.started + timedelta(seconds=10),
            "active_ended_at": self.started + timedelta(seconds=20),
            "recovery_completed_at": self.started + timedelta(seconds=30),
        }
        self.resources = [
            {
                "id": 1,
                "kind": 2,
                "deployment_name": "payment",
                "first_seen_at": self.started - timedelta(minutes=1),
                "last_seen_at": self.started + timedelta(minutes=1),
            },
            {
                "id": 2,
                "kind": 4,
                "deployment_name": "payment",
                "first_seen_at": self.started - timedelta(minutes=1),
                "last_seen_at": self.started + timedelta(minutes=1),
            },
        ]

    def test_builds_phase_relative_resource_buckets_and_keeps_missing_values(self) -> None:
        samples = [
            {
                "sampled_at": self.started + timedelta(seconds=12),
                "resource_id": 2,
                "cpu_usage_percent": 95.0,
                "cpu_limit_cores": 0.25,
                "cpu_throttled_usec_delta": 4000,
                "memory_current_bytes": 100,
                "memory_limit_bytes": 200,
                "memory_usage_percent": 50.0,
                "restart_count": 0,
                "ready": True,
                "extra_metrics": {"cpu_request_cores": 0.05},
            },
            {
                "sampled_at": self.started + timedelta(seconds=14),
                "resource_id": 2,
                "cpu_usage_percent": 100.0,
                "cpu_limit_cores": 0.25,
                "cpu_throttled_usec_delta": 6000,
                "memory_current_bytes": 120,
                "memory_limit_bytes": 200,
                "memory_usage_percent": 60.0,
                "restart_count": 0,
                "ready": True,
                "extra_metrics": {"cpu_request_cores": 0.05},
            },
        ]
        spans = [
            {
                "started_at": self.started + timedelta(seconds=13),
                "span_kind": 2,
                "duration_us": 100_000,
                "status_code": 0,
                "http_status_code": 200,
                "error_type": None,
                "deployment_name": "payment",
            },
            {
                "started_at": self.started + timedelta(seconds=14),
                "span_kind": 2,
                "duration_us": 500_000,
                "status_code": 2,
                "http_status_code": 500,
                "error_type": "server_error",
                "deployment_name": "payment",
            },
        ]
        rows = build_feature_rows(
            self.experiment, self.resources, samples, spans, []
        )
        self.assertEqual(len(rows), 12)
        container = next(
            row for row in rows
            if row["phase"] == "active"
            and row["bucket_start"] == self.started + timedelta(seconds=10)
            and row["resource_id"] == 2
        )
        self.assertEqual(container["sample_count"], 2)
        self.assertEqual(container["cpu_usage_average"], 97.5)
        self.assertEqual(container["cpu_usage_maximum"], 100.0)
        self.assertEqual(container["cpu_throttled_usec"], 10_000)
        deployment = next(
            row for row in rows
            if row["phase"] == "active"
            and row["bucket_start"] == self.started + timedelta(seconds=10)
            and row["resource_id"] == 1
        )
        self.assertEqual(deployment["sample_count"], 0)
        self.assertIsNone(deployment["cpu_usage_average"])
        self.assertEqual(deployment["server_request_count"], 2)
        self.assertEqual(deployment["server_error_count"], 1)
        self.assertEqual(deployment["server_latency_p50_ms"], 300.0)

    def test_percentile_matches_continuous_interpolation(self) -> None:
        self.assertIsNone(percentile([], 0.95))
        self.assertEqual(percentile([100, 200, 300], 0.50), 200.0)
        self.assertEqual(percentile([100, 200, 300], 0.95), 290.0)


if __name__ == "__main__":
    unittest.main()
