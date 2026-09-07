import os
import uuid

import httpx
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import HTMLResponse

from telemetry import configure_telemetry, current_trace_id, get_tracer


app = FastAPI(title="Infrastructure Debugger Demo Frontend")
checkout_url = os.getenv("CHECKOUT_URL", "http://checkout:8000")
http_client = httpx.Client(timeout=3.0)
tracer = get_tracer(__name__)


def call_checkout(request_id: str) -> dict:
    try:
        response = http_client.post(
            f"{checkout_url}/checkout",
            json={"item": "demo-item", "quantity": 1},
            headers={"X-Request-ID": request_id},
        )
        response.raise_for_status()
        return response.json()
    except httpx.HTTPError as error:
        raise HTTPException(status_code=502, detail=f"checkout unavailable: {error}") from error


@app.get("/", response_class=HTMLResponse)
def index() -> str:
    return """
    <!doctype html>
    <html lang="en">
      <head><meta charset="utf-8"><title>Infrastructure Debugger Demo</title></head>
      <body>
        <h1>Infrastructure Debugger Demo</h1>
        <p>This request flows through frontend → checkout → payment.</p>
        <button onclick="runOrder()">Run demo order</button>
        <pre id="result">Press the button to start.</pre>
        <script>
          async function runOrder() {
            const result = document.getElementById('result');
            result.textContent = 'Loading...';
            const response = await fetch('/api/order');
            result.textContent = JSON.stringify(await response.json(), null, 2);
          }
        </script>
      </body>
    </html>
    """


@app.get("/api/order")
def create_order(request: Request) -> dict:
    request_id = request.headers.get("X-Request-ID", str(uuid.uuid4()))
    with tracer.start_as_current_span("create demo order") as span:
        span.set_attribute("app.request_id", request_id)
        trace_id = current_trace_id()
        print(
            f"service=frontend request_id={request_id} trace_id={trace_id} action=create_order",
            flush=True,
        )
        return {
            "service": "frontend",
            "request_id": request_id,
            "trace_id": trace_id,
            "checkout": call_checkout(request_id),
        }


@app.get("/healthz")
def health() -> dict:
    return {"status": "ok", "service": "frontend"}


configure_telemetry(app, "frontend")
