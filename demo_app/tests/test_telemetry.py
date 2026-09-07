import os
import sys
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


DEMO_APP_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(DEMO_APP_DIRECTORY))


class _OtlpHandler(BaseHTTPRequestHandler):
    received_paths: list[str] = []
    received_payloads: dict[str, list[bytes]] = {}
    received_traceparent: str | None = None

    def do_POST(self) -> None:
        payload = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        self.received_paths.append(self.path)
        self.received_payloads.setdefault(self.path, []).append(payload)
        self.send_response(200)
        self.send_header("Content-Type", "application/x-protobuf")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self) -> None:
        type(self).received_traceparent = self.headers.get("traceparent")
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, format: str, *args) -> None:
        return


class TelemetryIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.collector = ThreadingHTTPServer(("127.0.0.1", 0), _OtlpHandler)
        cls.collector_thread = threading.Thread(target=cls.collector.serve_forever, daemon=True)
        cls.collector_thread.start()

        endpoint = f"http://127.0.0.1:{cls.collector.server_port}"
        cls.test_endpoint = endpoint
        os.environ["OTEL_EXPORTER_OTLP_ENDPOINT"] = endpoint
        os.environ["OTEL_METRIC_EXPORT_INTERVAL_MS"] = "100"
        os.environ["POD_NAME"] = "payment-test-pod"
        os.environ["POD_UID"] = "payment-test-uid"
        os.environ["POD_NAMESPACE"] = "infrastructure-demo"
        os.environ["NODE_NAME"] = "test-node"
        os.environ["CONTAINER_NAME"] = "payment"

        from payment.main import app

        from fastapi.testclient import TestClient

        cls.client = TestClient(app)

    @classmethod
    def tearDownClass(cls) -> None:
        from opentelemetry import metrics, trace

        metrics.get_meter_provider().force_flush()
        trace.get_tracer_provider().force_flush()
        metrics.get_meter_provider().shutdown()
        trace.get_tracer_provider().shutdown()
        cls.collector.shutdown()
        cls.collector.server_close()

    def test_request_has_trace_id_and_exports_traces_and_metrics(self) -> None:
        response = self.client.post(
            "/pay",
            headers={"X-Request-ID": "test-request"},
            json={"amount_cents": 1999},
        )

        self.assertEqual(200, response.status_code)
        trace_id = response.json()["trace_id"]
        self.assertEqual(32, len(trace_id))
        int(trace_id, 16)

        deadline = time.monotonic() + 2
        while time.monotonic() < deadline and "/v1/metrics" not in _OtlpHandler.received_paths:
            time.sleep(0.05)

        from opentelemetry import trace

        trace.get_tracer_provider().force_flush()
        self.assertIn("/v1/traces", _OtlpHandler.received_paths)
        self.assertIn("/v1/metrics", _OtlpHandler.received_paths)

        from opentelemetry.proto.collector.trace.v1.trace_service_pb2 import (
            ExportTraceServiceRequest,
        )

        trace_exports = [
            ExportTraceServiceRequest.FromString(payload)
            for payload in _OtlpHandler.received_payloads["/v1/traces"]
        ]
        resource_spans = [
            resource_span
            for export in trace_exports
            for resource_span in export.resource_spans
        ]
        resource_attributes = {
            attribute.key: attribute.value.string_value
            for resource_span in resource_spans
            for attribute in resource_span.resource.attributes
        }
        span_names = {
            span.name
            for resource_span in resource_spans
            for scope_spans in resource_span.scope_spans
            for span in scope_spans.spans
        }
        self.assertEqual("payment", resource_attributes["service.name"])
        self.assertEqual("payment-test-pod", resource_attributes["k8s.pod.name"])
        self.assertEqual("test-node", resource_attributes["k8s.node.name"])
        self.assertIn("authorize payment", span_names)

        from opentelemetry.proto.collector.metrics.v1.metrics_service_pb2 import (
            ExportMetricsServiceRequest,
        )
        from opentelemetry.proto.collector.trace.v1.trace_service_pb2 import (
            ExportTraceServiceRequest,
        )

        trace_exports = [
            ExportTraceServiceRequest.FromString(payload)
            for payload in _OtlpHandler.received_payloads["/v1/traces"]
        ]
        resource_spans = [
            resource_span
            for export in trace_exports
            for resource_span in export.resource_spans
        ]
        resource_attributes = {
            attribute.key: attribute.value.string_value
            for attribute in resource_spans[0].resource.attributes
        }
        self.assertEqual("payment", resource_attributes["service.name"])
        self.assertEqual("payment-test-pod", resource_attributes["k8s.pod.name"])
        self.assertEqual("test-node", resource_attributes["k8s.node.name"])

        span_names = {
            span.name
            for resource_span in resource_spans
            for scope_spans in resource_span.scope_spans
            for span in scope_spans.spans
        }
        self.assertIn("authorize payment", span_names)
        self.assertIn("POST /pay", span_names)

        metric_exports = [
            ExportMetricsServiceRequest.FromString(payload)
            for payload in _OtlpHandler.received_payloads["/v1/metrics"]
        ]
        metric_names = {
            metric.name
            for export in metric_exports
            for resource_metrics in export.resource_metrics
            for scope_metrics in resource_metrics.scope_metrics
            for metric in scope_metrics.metrics
        }
        self.assertIn("demo.http.server.request.count", metric_names)
        self.assertIn("demo.http.server.request.duration", metric_names)

    def test_httpx_instrumentation_injects_w3c_trace_context(self) -> None:
        import httpx
        from opentelemetry import trace

        client = httpx.Client()
        with trace.get_tracer(__name__).start_as_current_span("propagation test") as span:
            expected_trace_id = f"{span.get_span_context().trace_id:032x}"
            client.get(f"{self.test_endpoint}/downstream")

        traceparent = _OtlpHandler.received_traceparent
        self.assertIsNotNone(traceparent)
        self.assertEqual(expected_trace_id, traceparent.split("-")[1])


if __name__ == "__main__":
    unittest.main()
