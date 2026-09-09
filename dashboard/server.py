#!/usr/bin/env python3
"""Telemetry ingestion, query API, and infrastructure dashboard."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import logging
import os
from pathlib import Path
from typing import Any

import uvicorn
from fastapi import FastAPI, HTTPException, Query, Request, Response
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from google.protobuf.message import DecodeError

from database import TelemetryDatabase


DASHBOARD_ROOT = Path(__file__).resolve().parent
STATIC_ROOT = DASHBOARD_ROOT / "static"
MIGRATIONS_ROOT = DASHBOARD_ROOT / "migrations"
DEFAULT_SNAPSHOT_FILE = Path(
    os.environ.get(
        "TELEMETRY_SNAPSHOT_FILE",
        "/var/lib/ai-infrastructure-debugger/snapshot.json",
    )
)
DATABASE_URL = os.environ.get("DATABASE_URL")
LOGGER = logging.getLogger(__name__)


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


async def ingest_snapshots(
    database: TelemetryDatabase, snapshot_path: Path, stop: asyncio.Event
) -> None:
    last_timestamp: int | None = None
    while not stop.is_set():
        try:
            if not database.ready:
                await asyncio.to_thread(database.open)
            snapshot = load_snapshot(snapshot_path)
            timestamp = int(snapshot["node"]["timestamp_unix_ms"])
            if timestamp != last_timestamp:
                await asyncio.to_thread(database.ingest_snapshot, snapshot)
                last_timestamp = timestamp
        except Exception as error:  # Keep live file serving available during DB outages.
            LOGGER.warning("telemetry persistence unavailable: %s", error)
        try:
            await asyncio.wait_for(stop.wait(), timeout=1.0)
        except TimeoutError:
            pass


def create_app(
    snapshot_path: Path = DEFAULT_SNAPSHOT_FILE,
    database_url: str | None = DATABASE_URL,
    database_override: TelemetryDatabase | None = None,
) -> FastAPI:
    database = database_override or (
        TelemetryDatabase(database_url, MIGRATIONS_ROOT) if database_url else None
    )
    stop = asyncio.Event()

    @contextlib.asynccontextmanager
    async def lifespan(_: FastAPI):
        task = (
            asyncio.create_task(ingest_snapshots(database, snapshot_path, stop))
            if database
            else None
        )
        yield
        stop.set()
        if task:
            await task
        if database:
            await asyncio.to_thread(database.close)

    app = FastAPI(
        title="Infrastructure Debugger",
        docs_url=None,
        redoc_url=None,
        lifespan=lifespan,
    )
    app.state.telemetry_database = database

    @app.get("/api/health")
    def health() -> dict[str, Any]:
        return {
            "status": "ok",
            "snapshot_file": str(snapshot_path),
            "database_configured": database is not None,
            "database_ready": bool(database and database.ready),
        }

    @app.get("/api/ready")
    def ready() -> dict[str, str]:
        if database and database.ready:
            return {"status": "ready"}
        raise HTTPException(status_code=503, detail="Telemetry database is unavailable")

    @app.get("/api/snapshot")
    def snapshot() -> dict[str, Any]:
        return load_snapshot(snapshot_path)

    @app.post("/v1/traces")
    async def ingest_traces(request: Request) -> Response:
        if not database or not database.ready:
            raise HTTPException(status_code=503, detail="Telemetry database is unavailable")
        payload = await request.body()
        try:
            await asyncio.to_thread(database.ingest_otlp_traces, payload)
        except DecodeError as error:
            raise HTTPException(status_code=400, detail="Invalid OTLP trace payload") from error
        except Exception as error:
            LOGGER.exception("failed to persist OTLP traces")
            raise HTTPException(status_code=503, detail="Telemetry database is unavailable") from error
        return Response(content=b"", media_type="application/x-protobuf")

    @app.get("/api/rollups/overview")
    def overview_rollups(minutes: int = Query(60, ge=1, le=1440)) -> dict[str, Any]:
        if not database or not database.ready:
            raise HTTPException(status_code=503, detail="Historical telemetry is unavailable")
        try:
            return database.query_overview(minutes)
        except Exception as error:
            LOGGER.exception("failed to query overview rollups")
            raise HTTPException(status_code=503, detail="Historical telemetry is unavailable") from error

    @app.get("/api/rollups/deployments/{namespace}/{deployment}")
    def deployment_rollups(
        namespace: str,
        deployment: str,
        minutes: int = Query(60, ge=1, le=1440),
    ) -> dict[str, Any]:
        if not database or not database.ready:
            raise HTTPException(status_code=503, detail="Historical telemetry is unavailable")
        try:
            result = database.query_deployment(namespace, deployment, minutes)
        except Exception as error:
            LOGGER.exception("failed to query deployment rollups")
            raise HTTPException(status_code=503, detail="Historical telemetry is unavailable") from error
        if result is None:
            raise HTTPException(status_code=404, detail="Deployment not found")
        return result

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
    parser.add_argument("--database-url", default=DATABASE_URL)
    args = parser.parse_args()
    uvicorn.run(
        create_app(args.snapshot_file, args.database_url),
        host=args.host,
        port=args.port,
    )


if __name__ == "__main__":
    main()
