import os

import httpx
from fastapi import FastAPI, Header, HTTPException
from pydantic import BaseModel, Field

from telemetry import configure_telemetry, current_trace_id, get_tracer


app = FastAPI(title="Infrastructure Debugger Demo Checkout")
payment_url = os.getenv("PAYMENT_URL", "http://payment:8000")
http_client = httpx.Client(timeout=3.0)
tracer = get_tracer(__name__)


class CheckoutRequest(BaseModel):
    item: str = Field(min_length=1)
    quantity: int = Field(gt=0)


def call_payment(request_id: str, amount: int) -> dict:
    try:
        response = http_client.post(
            f"{payment_url}/pay",
            json={"amount_cents": amount},
            headers={"X-Request-ID": request_id},
        )
        response.raise_for_status()
        return response.json()
    except httpx.HTTPError as error:
        raise HTTPException(status_code=502, detail=f"payment unavailable: {error}") from error


@app.post("/checkout")
def checkout(
    order: CheckoutRequest,
    x_request_id: str = Header(..., alias="X-Request-ID"),
) -> dict:
    with tracer.start_as_current_span("calculate and submit checkout") as span:
        amount_cents = 1999 * order.quantity
        span.set_attribute("app.request_id", x_request_id)
        span.set_attribute("order.item", order.item)
        span.set_attribute("order.quantity", order.quantity)
        span.set_attribute("order.amount_cents", amount_cents)
        trace_id = current_trace_id()
        print(
            f"service=checkout request_id={x_request_id} trace_id={trace_id} action=checkout",
            flush=True,
        )
        return {
            "service": "checkout",
            "request_id": x_request_id,
            "trace_id": trace_id,
            "item": order.item,
            "quantity": order.quantity,
            "amount_cents": amount_cents,
            "payment": call_payment(x_request_id, amount_cents),
        }


@app.get("/healthz")
def health() -> dict:
    return {"status": "ok", "service": "checkout"}


configure_telemetry(app, "checkout")
