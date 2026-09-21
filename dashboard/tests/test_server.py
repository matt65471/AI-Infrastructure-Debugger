import sys
import tempfile
import unittest
import gzip
import copy
import json
import uuid
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

from fastapi import HTTPException
from fastapi.testclient import TestClient
from opentelemetry.proto.collector.trace.v1.trace_service_pb2 import (
    ExportTraceServiceRequest,
)


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from server import MAX_INFRA_PAYLOAD_BYTES, create_app, load_snapshot  # noqa: E402


class FakeDatabase:
    def __init__(self) -> None:
        self.ready = True
        self.closed = False
        self.snapshots: list[dict[str, Any]] = []
        self.trace_payloads: list[bytes] = []
        self.experiments: dict[str, dict[str, Any]] = {}

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
            "node_summary": {},
            "deployments": [],
            "events": [],
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
            "summary": {},
            "series": [],
            "routes": [],
        }

    def create_fault_experiment(self, **values: Any) -> dict[str, Any]:
        identifier = str(values.pop("experiment_id"))
        result = {
            "id": identifier,
            **values,
            "status": "baseline",
        }
        for key, value in list(result.items()):
            if isinstance(value, datetime):
                result[key] = value.isoformat()
        self.experiments[identifier] = result
        return result

    def update_fault_experiment(
        self,
        experiment_id: uuid.UUID,
        *,
        status: str,
        observed_at: datetime,
        error_message: str | None = None,
        traffic_summary: dict[str, Any] | None = None,
    ) -> dict[str, Any] | None:
        result = self.experiments.get(str(experiment_id))
        if result is None:
            return None
        result["status"] = status
        result["observed_at"] = observed_at.isoformat()
        result["error_message"] = error_message
        if traffic_summary is not None:
            result["traffic_summary"] = traffic_summary
        return result


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

    def test_dashboard_assets_define_live_and_historical_modes(self) -> None:
        static_root = Path(__file__).resolve().parents[1] / "static"
        index = (static_root / "index.html").read_text(encoding="utf-8")
        javascript = (static_root / "app.js").read_text(encoding="utf-8")
        self.assertIn('id="live-mode"', index)
        self.assertIn('id="history-mode"', index)
        self.assertIn("#/history/60", index)
        self.assertIn("new Set([60, 360, 1440])", javascript)
        self.assertIn("if (currentRoute().mode !== \"live\") return", javascript)


class DashboardApiTest(unittest.TestCase):
    def setUp(self) -> None:
        self.database = FakeDatabase()
        self.sample_path = Path(__file__).resolve().parents[1] / "sample_snapshot.json"
        self.sample = load_snapshot(self.sample_path)
        self.sample["node"]["timestamp_unix_ms"] = int(datetime.now(UTC).timestamp() * 1000)
        self.client_context = TestClient(
            create_app(
                database_override=self.database,
                ingestion_token="test-token",
                initial_snapshot=self.sample,
            )
        )
        self.client = self.client_context.__enter__()

    def tearDown(self) -> None:
        self.client_context.__exit__(None, None, None)

    def test_current_snapshot_remains_available(self) -> None:
        response = self.client.get("/api/snapshot")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["node"]["hostname"], "ubuntu-vm")

    def test_live_endpoint_waits_for_first_persisted_ingestion(self) -> None:
        with TestClient(create_app(database_override=FakeDatabase(), ingestion_token="token")) as client:
            self.assertEqual(client.get("/api/snapshot").status_code, 503)
            health = client.get("/api/health").json()
            self.assertTrue(health["live_stale"])
            self.assertIsNone(health["latest_snapshot_timestamp_unix_ms"])

    def envelope(self, *snapshots: dict[str, Any]) -> dict[str, Any]:
        return {
            "schema_version": 1,
            "collector_id": "test-machine",
            "snapshots": list(snapshots or (self.sample,)),
        }

    def post_snapshots(self, payload: dict[str, Any], token: str = "test-token"):
        return self.client.post(
            "/v1/infra-snapshots",
            content=json.dumps(payload),
            headers={"authorization": f"Bearer {token}", "content-type": "application/json"},
        )

    def experiment_payload(self) -> dict[str, Any]:
        started = datetime.now(UTC)
        return {
            "id": str(uuid.uuid4()),
            "fault_type": "cpu_saturation",
            "namespace_name": "infrastructure-demo",
            "target_kind": "deployment",
            "target_name": "payment",
            "parameters": {"duration_seconds": 30},
            "baseline_started_at": started.isoformat(),
            "expires_at": (started + timedelta(minutes=3)).isoformat(),
        }

    def test_infrastructure_ingestion_requires_auth_and_updates_live_after_database(self) -> None:
        self.assertEqual(self.client.post("/v1/infra-snapshots", json=self.envelope()).status_code, 401)
        self.assertEqual(self.post_snapshots(self.envelope(), "wrong").status_code, 401)
        unsupported = self.client.post(
            "/v1/infra-snapshots",
            content=b"{}",
            headers={"authorization": "Bearer test-token", "content-type": "text/plain"},
        )
        self.assertEqual(unsupported.status_code, 415)
        newer = copy.deepcopy(self.sample)
        newer["node"]["timestamp_unix_ms"] += 1000
        response = self.post_snapshots(self.envelope(newer))
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["accepted"], 1)
        self.assertEqual(self.database.snapshots[-1], newer)
        self.assertEqual(self.client.get("/api/snapshot").json()["node"]["timestamp_unix_ms"], newer["node"]["timestamp_unix_ms"])

    def test_infrastructure_ingestion_validates_shape_count_and_time(self) -> None:
        self.assertEqual(self.post_snapshots({}).status_code, 422)
        self.assertEqual(self.post_snapshots(self.envelope(*([self.sample] * 11))).status_code, 422)
        old = copy.deepcopy(self.sample)
        old["node"]["timestamp_unix_ms"] = int((datetime.now(UTC) - timedelta(days=8)).timestamp() * 1000)
        future = copy.deepcopy(self.sample)
        future["node"]["timestamp_unix_ms"] = int((datetime.now(UTC) + timedelta(minutes=6)).timestamp() * 1000)
        self.assertEqual(self.post_snapshots(self.envelope(old)).status_code, 422)
        self.assertEqual(self.post_snapshots(self.envelope(future)).status_code, 422)

    def test_infrastructure_ingestion_rejects_oversized_payload(self) -> None:
        response = self.client.post(
            "/v1/infra-snapshots",
            content=b"x" * (MAX_INFRA_PAYLOAD_BYTES + 1),
            headers={"authorization": "Bearer test-token", "content-type": "application/json"},
        )
        self.assertEqual(response.status_code, 413)

    def test_fault_experiment_records_lifecycle_with_authentication(self) -> None:
        payload = self.experiment_payload()
        self.assertEqual(
            self.client.post("/v1/fault-experiments", json=payload).status_code,
            401,
        )
        created = self.client.post(
            "/v1/fault-experiments",
            json=payload,
            headers={"authorization": "Bearer test-token"},
        )
        self.assertEqual(created.status_code, 201)
        self.assertEqual(created.json()["status"], "baseline")
        updated = self.client.patch(
            f"/v1/fault-experiments/{payload['id']}",
            json={"status": "injecting", "observed_at": datetime.now(UTC).isoformat()},
            headers={"authorization": "Bearer test-token"},
        )
        self.assertEqual(updated.status_code, 200)
        self.assertEqual(updated.json()["status"], "injecting")

    def test_fault_experiment_rejects_unsafe_scope_and_duration(self) -> None:
        payload = self.experiment_payload()
        payload["namespace_name"] = "default"
        response = self.client.post(
            "/v1/fault-experiments",
            json=payload,
            headers={"authorization": "Bearer test-token"},
        )
        self.assertEqual(response.status_code, 422)
        payload = self.experiment_payload()
        payload["expires_at"] = (
            datetime.now(UTC) + timedelta(minutes=11)
        ).isoformat()
        response = self.client.post(
            "/v1/fault-experiments",
            json=payload,
            headers={"authorization": "Bearer test-token"},
        )
        self.assertEqual(response.status_code, 422)

    def test_older_replay_does_not_replace_newer_live_snapshot(self) -> None:
        newer = copy.deepcopy(self.sample)
        newer["node"]["timestamp_unix_ms"] += 5000
        self.assertEqual(self.post_snapshots(self.envelope(newer)).status_code, 200)
        self.assertEqual(self.post_snapshots(self.envelope(self.sample)).status_code, 200)
        current = self.client.get("/api/snapshot").json()
        self.assertEqual(current["node"]["timestamp_unix_ms"], newer["node"]["timestamp_unix_ms"])

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

    def test_database_outage_freezes_live_and_disables_ingestion_and_history(self) -> None:
        self.database.ready = False
        self.assertEqual(self.client.get("/api/snapshot").status_code, 200)
        self.assertEqual(self.client.get("/api/rollups/overview").status_code, 503)
        self.assertEqual(self.client.post("/v1/traces", content=b"").status_code, 503)
        self.assertEqual(self.post_snapshots(self.envelope()).status_code, 503)

    def test_invalid_snapshot_shape_returns_service_unavailable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            snapshot_path = Path(directory) / "snapshot.json"
            snapshot_path.write_text("[]", encoding="utf-8")
            with self.assertRaises(HTTPException) as context:
                load_snapshot(snapshot_path)
        self.assertEqual(context.exception.status_code, 503)


if __name__ == "__main__":
    unittest.main()
