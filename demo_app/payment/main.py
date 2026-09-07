from fastapi import FastAPI, Header
from pydantic import BaseModel, Field

from telemetry import configure_telemetry, current_trace_id, get_tracer


app = FastAPI(title="Infrastructure Debugger Demo Payment")
tracer = get_tracer(__name__)


class PaymentRequest(BaseModel):
    amount_cents: int = Field(gt=0)


@app.post("/pay")
def pay(
    payment: PaymentRequest,
    x_request_id: str = Header(..., alias="X-Request-ID"),
) -> dict:
    with tracer.start_as_current_span("authorize payment") as span:
        span.set_attribute("app.request_id", x_request_id)
        span.set_attribute("payment.amount_cents", payment.amount_cents)
        span.set_attribute("payment.outcome", "approved")
        trace_id = current_trace_id()
        print(
            f"service=payment request_id={x_request_id} trace_id={trace_id} action=pay",
            flush=True,
        )
        return {
            "service": "payment",
            "request_id": x_request_id,
            "trace_id": trace_id,
            "status": "approved",
            "amount_cents": payment.amount_cents,
        }


@app.get("/healthz")
def health() -> dict:
    return {"status": "ok", "service": "payment"}


configure_telemetry(app, "payment")
