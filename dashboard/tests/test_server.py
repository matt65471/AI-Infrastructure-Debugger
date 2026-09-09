import sys
import tempfile
import unittest
import gzip
from pathlib import Path
from typing import Any

from fastapi import HTTPException
from fastapi.testclient import TestClient
from opentelemetry.proto.collector.trace.v1.trace_service_pb2 import (
    ExportTraceServiceRequest,
)


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from server import create_app, load_snapshot  # noqa: E402


class FakeDatabase:
    def __init__(self) -> None:
        self.ready = True
        self.closed = False
        self.snapshots: list[dict[str, Any]] = []
        self.trace_payloads: list[bytes] = []

    def open(self) -> None:
        self.ready = True

    def close(self) -> None:
        self.closed = True
        self.ready = False

    def ingest_snapshot(self, snapshot: dict[str, Any]) -> None:
        self.snapshots.append(snapshot)

    def ingest_otlp_traces(self, payload: bytes) -> int:
        ExportTraceServiceRequest.FromString(payload)
        self.trace_payloads.append(payload)
        return 0

    def query_overview(self, minutes: int) -> dict[str, Any]:
        return {
            "window_minutes": minutes,
            "start": "2026-09-08T00:00:00+00:00",
            "end": "2026-09-08T01:00:00+00:00",
            "node": [],
            "deployments": [],
        }

    def query_deployment(
        self, namespace: str, deployment: str, minutes: int
    ) -> dict[str, Any] | None:
        if deployment == "unknown":
            return None
        return {
            "namespace": namespace,
            "deployment_name": deployment,
            "window_minutes": minutes,
            "series": [],
            "routes": [],
        }


class SnapshotLoadingTest(unittest.TestCase):
    def test_loads_the_representative_snapshot(self) -> None:
        sample_path = Path(__file__).resolve().parents[1] / "sample_snapshot.json"
        snapshot = load_snapshot(sample_path)
        self.assertEqual(snapshot["node"]["hostname"], "ubuntu-vm")
        self.assertEqual(len(snapshot["deployments"]), 3)

    def test_missing_snapshot_returns_service_unavailable(self) -> None:
        with self.assertRaises(HTTPException) as context:
            load_snapshot(Path("/tmp/does-not-exist-telemetry-snapshot.json"))
        self.assertEqual(context.exception.status_code, 503)


class DashboardApiTest(unittest.TestCase):
    def setUp(self) -> None:
        self.database = FakeDatabase()
        self.sample_path = Path(__file__).resolve().parents[1] / "sample_snapshot.json"
        self.client_context = TestClient(
            create_app(
                snapshot_path=self.sample_path,
                database_override=self.database,
            )
        )
        self.client = self.client_context.__enter__()

    def tearDown(self) -> None:
        self.client_context.__exit__(None, None, None)

    def test_current_snapshot_remains_available(self) -> None:
        response = self.client.get("/api/snapshot")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["node"]["hostname"], "ubuntu-vm")

    def test_overview_validates_window_and_returns_database_result(self) -> None:
        self.assertEqual(
            self.client.get("/api/rollups/overview?minutes=0").status_code,
            422,
        )
        self.assertEqual(
            self.client.get("/api/rollups/overview?minutes=1441").status_code,
            422,
        )
        response = self.client.get("/api/rollups/overview?minutes=17")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["window_minutes"], 17)

    def test_deployment_endpoint_distinguishes_known_and_unknown(self) -> None:
        known = self.client.get(
            "/api/rollups/deployments/infrastructure-demo/payment?minutes=60"
        )
        missing = self.client.get(
            "/api/rollups/deployments/infrastructure-demo/unknown?minutes=60"
        )
        self.assertEqual(known.status_code, 200)
        self.assertEqual(known.json()["series"], [])
        self.assertEqual(missing.status_code, 404)

    def test_otlp_endpoint_accepts_protobuf_and_rejects_invalid_payloads(self) -> None:
        payload = ExportTraceServiceRequest().SerializeToString()
        accepted = self.client.post(
            "/v1/traces",
            content=payload,
            headers={"content-type": "application/x-protobuf"},
        )
        rejected = self.client.post(
            "/v1/traces",
            content=b"not protobuf",
            headers={"content-type": "application/x-protobuf"},
        )
        self.assertEqual(accepted.status_code, 200)
        self.assertEqual(accepted.headers["content-type"], "application/x-protobuf")
        self.assertEqual(rejected.status_code, 400)

    def test_otlp_endpoint_accepts_collector_gzip_compression(self) -> None:
        payload = ExportTraceServiceRequest().SerializeToString()
        response = self.client.post(
            "/v1/traces",
            content=gzip.compress(payload),
            headers={
                "content-type": "application/x-protobuf",
                "content-encoding": "gzip",
            },
        )
        self.assertEqual(response.status_code, 200)
        self.assertEqual(self.database.trace_payloads[-1], payload)

    def test_otlp_endpoint_rejects_unsupported_compression(self) -> None:
        response = self.client.post(
            "/v1/traces",
            content=b"payload",
            headers={"content-encoding": "br"},
        )
        self.assertEqual(response.status_code, 415)

    def test_database_outage_only_disables_historical_endpoints(self) -> None:
        self.database.ready = False
        self.assertEqual(self.client.get("/api/snapshot").status_code, 200)
        self.assertEqual(self.client.get("/api/rollups/overview").status_code, 503)
        self.assertEqual(self.client.post("/v1/traces", content=b"").status_code, 503)

    def test_invalid_snapshot_shape_returns_service_unavailable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            snapshot_path = Path(directory) / "snapshot.json"
            snapshot_path.write_text("[]", encoding="utf-8")
            with self.assertRaises(HTTPException) as context:
                load_snapshot(snapshot_path)
        self.assertEqual(context.exception.status_code, 503)


if __name__ == "__main__":
    unittest.main()
