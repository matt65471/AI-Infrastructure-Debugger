import sys
import tempfile
import unittest
from pathlib import Path

from fastapi import HTTPException


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from server import load_snapshot  # noqa: E402


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

    def test_invalid_snapshot_shape_returns_service_unavailable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            snapshot_path = Path(directory) / "snapshot.json"
            snapshot_path.write_text("[]", encoding="utf-8")
            with self.assertRaises(HTTPException) as context:
                load_snapshot(snapshot_path)
        self.assertEqual(context.exception.status_code, 503)


if __name__ == "__main__":
    unittest.main()
