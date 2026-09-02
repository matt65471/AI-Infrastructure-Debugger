#!/usr/bin/env python3
"""Read-only local dashboard server for telemetry snapshots."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any

import uvicorn
from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles


DASHBOARD_ROOT = Path(__file__).resolve().parent
STATIC_ROOT = DASHBOARD_ROOT / "static"
DEFAULT_SNAPSHOT_FILE = Path(
    os.environ.get(
        "TELEMETRY_SNAPSHOT_FILE",
        "/tmp/ai-infrastructure-debugger-snapshot.json",
    )
)


def load_snapshot(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as snapshot_file:
            snapshot = json.load(snapshot_file)
    except FileNotFoundError as error:
        raise HTTPException(
            status_code=503,
            detail=f"Waiting for telemetry snapshot at {path}",
        ) from error
    except (OSError, json.JSONDecodeError) as error:
        raise HTTPException(
            status_code=503,
            detail=f"Telemetry snapshot is not readable yet: {error}",
        ) from error

    if not isinstance(snapshot, dict) or not isinstance(snapshot.get("node"), dict):
        raise HTTPException(status_code=503, detail="Telemetry snapshot has an invalid shape")
    return snapshot


def create_app(snapshot_path: Path = DEFAULT_SNAPSHOT_FILE) -> FastAPI:
    app = FastAPI(
        title="Infrastructure Debugger",
        docs_url=None,
        redoc_url=None,
    )

    @app.get("/api/health")
    def health() -> dict[str, str]:
        return {"status": "ok", "snapshot_file": str(snapshot_path)}

    @app.get("/api/snapshot")
    def snapshot() -> dict[str, Any]:
        return load_snapshot(snapshot_path)

    @app.get("/")
    def index() -> FileResponse:
        return FileResponse(STATIC_ROOT / "index.html")

    app.mount("/static", StaticFiles(directory=STATIC_ROOT), name="static")
    return app


app = create_app()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--snapshot-file", type=Path, default=DEFAULT_SNAPSHOT_FILE)
    args = parser.parse_args()
    uvicorn.run(create_app(args.snapshot_file), host=args.host, port=args.port)


if __name__ == "__main__":
    main()
