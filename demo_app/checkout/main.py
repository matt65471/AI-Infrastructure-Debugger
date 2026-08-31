import json
import os
import urllib.error
import urllib.request

from fastapi import FastAPI, Header, HTTPException
from pydantic import BaseModel, Field


app = FastAPI(title="Infrastructure Debugger Demo Checkout")
payment_url = os.getenv("PAYMENT_URL", "http://payment:8000")


class CheckoutRequest(BaseModel):
    item: str = Field(min_length=1)
    quantity: int = Field(gt=0)


def call_payment(request_id: str, amount: int) -> dict:
    request = urllib.request.Request(
        f"{payment_url}/pay",
        data=json.dumps({"amount_cents": amount}).encode(),
        headers={"Content-Type": "application/json", "X-Request-ID": request_id},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=3) as response:
            return json.load(response)
    except (urllib.error.URLError, TimeoutError) as error:
        raise HTTPException(status_code=502, detail=f"payment unavailable: {error}") from error


@app.post("/checkout")
def checkout(
    order: CheckoutRequest,
    x_request_id: str = Header(..., alias="X-Request-ID"),
) -> dict:
    amount_cents = 1999 * order.quantity
    print(f"service=checkout request_id={x_request_id} action=checkout", flush=True)
    return {
        "service": "checkout",
        "request_id": x_request_id,
        "item": order.item,
        "quantity": order.quantity,
        "payment": call_payment(x_request_id, amount_cents),
    }


@app.get("/healthz")
def health() -> dict:
    return {"status": "ok", "service": "checkout"}
