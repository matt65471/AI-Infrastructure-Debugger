import sys
import unittest
from datetime import UTC, datetime
from decimal import Decimal
from pathlib import Path

from opentelemetry.proto.common.v1.common_pb2 import AnyValue, KeyValue


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from database import (  # noqa: E402
    RESOURCE_CONTAINER,
    _rollup_json,
    protobuf_attributes,
    resource_uuid,
)


class DatabaseHelpersTest(unittest.TestCase):
    def test_resource_keys_are_deterministic_and_identity_sensitive(self) -> None:
        first = resource_uuid(RESOURCE_CONTAINER, "pod-uid", "payment")
        repeated = resource_uuid(RESOURCE_CONTAINER, "pod-uid", "payment")
        different = resource_uuid(RESOURCE_CONTAINER, "pod-uid", "checkout")
        self.assertEqual(first, repeated)
        self.assertNotEqual(first, different)

    def test_protobuf_attributes_convert_typed_values(self) -> None:
        attributes = protobuf_attributes(
            [
                KeyValue(key="name", value=AnyValue(string_value="payment")),
                KeyValue(key="count", value=AnyValue(int_value=3)),
                KeyValue(key="healthy", value=AnyValue(bool_value=True)),
            ]
        )
        self.assertEqual(
            attributes,
            {"name": "payment", "count": 3, "healthy": True},
        )

    def test_rollup_json_preserves_gaps_and_serializes_numeric_values(self) -> None:
        bucket = datetime(2026, 9, 8, tzinfo=UTC)
        gap = _rollup_json(bucket, None)
        populated = _rollup_json(
            bucket,
            {
                "request_count": 4,
                "error_count": 1,
                "average_latency_ms": Decimal("12.5"),
            },
        )
        self.assertIsNone(gap["request_count"])
        self.assertEqual(populated["average_latency_ms"], 12.5)
        self.assertEqual(populated["error_rate_percent"], 25.0)


if __name__ == "__main__":
    unittest.main()
