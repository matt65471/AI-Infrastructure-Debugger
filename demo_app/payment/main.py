from fastapi import FastAPI, Header
from pydantic import BaseModel, Field


app = FastAPI(title="Infrastructure Debugger Demo Payment")


class PaymentRequest(BaseModel):
    amount_cents: int = Field(gt=0)


@app.post("/pay")
def pay(
    payment: PaymentRequest,
    x_request_id: str = Header(..., alias="X-Request-ID"),
) -> dict:
    print(f"service=payment request_id={x_request_id} action=pay", flush=True)
    return {
        "service": "payment",
        "request_id": x_request_id,
        "status": "approved",
        "amount_cents": payment.amount_cents,
    }


@app.get("/healthz")
def health() -> dict:
    return {"status": "ok", "service": "payment"}
