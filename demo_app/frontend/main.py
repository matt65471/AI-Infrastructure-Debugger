import json
import os
import urllib.error
import urllib.request
import uuid

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import HTMLResponse


app = FastAPI(title="Infrastructure Debugger Demo Frontend")
checkout_url = os.getenv("CHECKOUT_URL", "http://checkout:8000")


def call_checkout(request_id: str) -> dict:
    request = urllib.request.Request(
        f"{checkout_url}/checkout",
        data=json.dumps({"item": "demo-item", "quantity": 1}).encode(),
        headers={"Content-Type": "application/json", "X-Request-ID": request_id},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=3) as response:
            return json.load(response)
    except (urllib.error.URLError, TimeoutError) as error:
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
    print(f"service=frontend request_id={request_id} action=create_order", flush=True)
    return {
        "service": "frontend",
        "request_id": request_id,
        "checkout": call_checkout(request_id),
    }


@app.get("/healthz")
def health() -> dict:
    return {"status": "ok", "service": "frontend"}
