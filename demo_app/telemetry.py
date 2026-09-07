"""Shared OpenTelemetry setup for the demo services."""

from __future__ import annotations

import os
from time import perf_counter

from fastapi import FastAPI, Request
from opentelemetry import metrics, trace
from opentelemetry.exporter.otlp.proto.http.metric_exporter import OTLPMetricExporter
from opentelemetry.exporter.otlp.proto.http.trace_exporter import OTLPSpanExporter
from opentelemetry.instrumentation.fastapi import FastAPIInstrumentor
from opentelemetry.instrumentation.httpx import HTTPXClientInstrumentor
from opentelemetry.metrics import set_meter_provider
from opentelemetry.sdk.metrics import MeterProvider
from opentelemetry.sdk.metrics.export import PeriodicExportingMetricReader
from opentelemetry.sdk.resources import Resource
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor
from opentelemetry.trace import set_tracer_provider


_configured = False


def _is_disabled() -> bool:
    return os.getenv("OTEL_SDK_DISABLED", "false").lower() == "true"


def _otlp_endpoint(signal: str) -> str:
    signal_endpoint = os.getenv(f"OTEL_EXPORTER_OTLP_{signal.upper()}_ENDPOINT")
    if signal_endpoint:
        return signal_endpoint
    base_endpoint = os.getenv("OTEL_EXPORTER_OTLP_ENDPOINT", "http://otel-collector:4318")
    return f"{base_endpoint.rstrip('/')}/v1/{signal}"


def _resource(service_name: str) -> Resource:
    attributes: dict[str, str] = {
        "service.name": service_name,
        "service.version": os.getenv("SERVICE_VERSION", "v1"),
        "deployment.environment.name": os.getenv("DEPLOYMENT_ENVIRONMENT", "demo"),
    }
    environment_attributes = {
        "service.instance.id": "POD_UID",
        "k8s.namespace.name": "POD_NAMESPACE",
        "k8s.pod.name": "POD_NAME",
        "k8s.node.name": "NODE_NAME",
        "k8s.container.name": "CONTAINER_NAME",
    }
    for attribute, environment_variable in environment_attributes.items():
        value = os.getenv(environment_variable)
        if value:
            attributes[attribute] = value
    return Resource.create(attributes)


def configure_telemetry(app: FastAPI, service_name: str) -> None:
    """Export distributed traces and request metrics for one FastAPI service."""
    global _configured
    if _configured or _is_disabled():
        return

    resource = _resource(service_name)

    tracer_provider = TracerProvider(resource=resource)
    tracer_provider.add_span_processor(
        BatchSpanProcessor(OTLPSpanExporter(endpoint=_otlp_endpoint("traces")))
    )
    set_tracer_provider(tracer_provider)

    metric_reader = PeriodicExportingMetricReader(
        OTLPMetricExporter(endpoint=_otlp_endpoint("metrics")),
        export_interval_millis=int(os.getenv("OTEL_METRIC_EXPORT_INTERVAL_MS", "5000")),
    )
    set_meter_provider(MeterProvider(resource=resource, metric_readers=[metric_reader]))

    HTTPXClientInstrumentor().instrument()
    FastAPIInstrumentor.instrument_app(app, excluded_urls="healthz")
    _add_request_metrics(app, service_name)
    _configured = True


def _add_request_metrics(app: FastAPI, service_name: str) -> None:
    meter = metrics.get_meter("infrastructure-debugger.demo", "1.0.0")
    request_count = meter.create_counter(
        "demo.http.server.request.count",
        unit="{request}",
        description="Number of application HTTP requests",
    )
    request_duration = meter.create_histogram(
        "demo.http.server.request.duration",
        unit="s",
        description="Application HTTP request duration",
    )
    active_requests = meter.create_up_down_counter(
        "demo.http.server.active_requests",
        unit="{request}",
        description="Number of application HTTP requests currently being handled",
    )

    @app.middleware("http")
    async def record_request_metrics(request: Request, call_next):
        if request.url.path == "/healthz":
            return await call_next(request)

        base_attributes = {
            "service.name": service_name,
            "http.request.method": request.method,
        }
        active_requests.add(1, base_attributes)
        started_at = perf_counter()
        status_code = 500
        try:
            response = await call_next(request)
            status_code = response.status_code
            return response
        finally:
            route = request.scope.get("route")
            route_path = getattr(route, "path", request.url.path)
            attributes = {
                **base_attributes,
                "http.route": route_path,
                "http.response.status_code": status_code,
            }
            active_requests.add(-1, base_attributes)
            request_count.add(1, attributes)
            request_duration.record(perf_counter() - started_at, attributes)


def get_tracer(module_name: str):
    return trace.get_tracer(module_name)


def current_trace_id() -> str | None:
    span_context = trace.get_current_span().get_span_context()
    if not span_context.is_valid:
        return None
    return f"{span_context.trace_id:032x}"
