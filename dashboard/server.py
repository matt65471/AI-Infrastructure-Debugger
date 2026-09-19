#!/usr/bin/env python3
"""Telemetry ingestion, query API, and infrastructure dashboard."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import copy
import gzip
import json
import logging
import os
import secrets
import threading
from datetime import UTC, datetime, timedelta
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
DATABASE_URL = os.environ.get("DATABASE_URL")
INGESTION_TOKEN = os.environ.get("TELEMETRY_INGEST_TOKEN")
MAX_INFRA_PAYLOAD_BYTES = 16 * 1024 * 1024
MAX_INFRA_SNAPSHOTS = 10
MAX_SNAPSHOT_AGE = timedelta(days=7)
MAX_SNAPSHOT_FUTURE = timedelta(minutes=5)
LIVE_STALE_AFTER_SECONDS = 10
LOGGER = logging.getLogger(__name__)


class LatestSnapshotStore:
    """Thread-safe latest persisted snapshot cache."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._snapshot: dict[str, Any] | None = None
        self._collector_id: str | None = None

    def update(self, collector_id: str, snapshot: dict[str, Any]) -> bool:
        timestamp = int(snapshot["node"]["timestamp_unix_ms"])
        with self._lock:
            current = int(self._snapshot["node"]["timestamp_unix_ms"]) if self._snapshot else -1
            if timestamp < current:
                return False
            self._snapshot = copy.deepcopy(snapshot)
            self._collector_id = collector_id
            return True

    def get(self) -> dict[str, Any] | None:
        with self._lock:
            return copy.deepcopy(self._snapshot)

    def status(self) -> dict[str, Any]:
        with self._lock:
            if not self._snapshot:
                return {
                    "collector_id": None,
                    "latest_snapshot_timestamp_unix_ms": None,
                    "latest_snapshot_age_seconds": None,
                    "live_stale": True,
                }
            timestamp = int(self._snapshot["node"]["timestamp_unix_ms"])
            age = max(0.0, datetime.now(UTC).timestamp() - timestamp / 1000)
            return {
                "collector_id": self._collector_id,
                "latest_snapshot_timestamp_unix_ms": timestamp,
                "latest_snapshot_age_seconds": round(age, 3),
                "live_stale": age > LIVE_STALE_AFTER_SECONDS,
            }


def load_snapshot(path: Path) -> dict[str, Any]:
    """Load a fixture for local UI development; production uses HTTP ingestion."""
    try:
        with path.open("r", encoding="utf-8") as snapshot_file:
            snapshot = json.load(snapshot_file)
    except FileNotFoundError as error:
        raise HTTPException(status_code=503, detail=f"Snapshot not found at {path}") from error
    except (OSError, json.JSONDecodeError) as error:
        raise HTTPException(status_code=503, detail=f"Snapshot is not readable: {error}") from error
    if not isinstance(snapshot, dict) or not isinstance(snapshot.get("node"), dict):
        raise HTTPException(status_code=503, detail="Snapshot has an invalid shape")
    return snapshot


def validate_infra_envelope(payload: Any) -> tuple[str, list[dict[str, Any]]]:
    if not isinstance(payload, dict):
        raise HTTPException(status_code=422, detail="Request body must be a JSON object")
    if payload.get("schema_version") != 1:
        raise HTTPException(status_code=422, detail="Unsupported infrastructure schema_version")
    collector_id = payload.get("collector_id")
    if not isinstance(collector_id, str) or not collector_id.strip() or len(collector_id) > 256:
        raise HTTPException(status_code=422, detail="collector_id is required")
    snapshots = payload.get("snapshots")
    if not isinstance(snapshots, list) or not snapshots:
        raise HTTPException(status_code=422, detail="snapshots must be a non-empty array")
    if len(snapshots) > MAX_INFRA_SNAPSHOTS:
        raise HTTPException(status_code=422, detail=f"At most {MAX_INFRA_SNAPSHOTS} snapshots are allowed")

    now = datetime.now(UTC)
    earliest = now - MAX_SNAPSHOT_AGE
    latest = now + MAX_SNAPSHOT_FUTURE
    array_fields = (
        "processes", "top_cpu_processes", "top_memory_processes", "containers",
        "pods", "deployments", "kubernetes_events",
    )
    validated: list[dict[str, Any]] = []
    for index, snapshot in enumerate(snapshots):
        if not isinstance(snapshot, dict) or not isinstance(snapshot.get("node"), dict):
            raise HTTPException(status_code=422, detail=f"snapshots[{index}].node is required")
        node = snapshot["node"]
        hostname = node.get("hostname")
        timestamp = node.get("timestamp_unix_ms")
        if not isinstance(hostname, str) or not hostname.strip():
            raise HTTPException(status_code=422, detail=f"snapshots[{index}].node.hostname is required")
        if isinstance(timestamp, bool) or not isinstance(timestamp, int):
            raise HTTPException(status_code=422, detail=f"snapshots[{index}].node.timestamp_unix_ms must be an integer")
        try:
            sampled_at = datetime.fromtimestamp(timestamp / 1000, tz=UTC)
        except (OverflowError, OSError, ValueError) as error:
            raise HTTPException(status_code=422, detail=f"snapshots[{index}] has an invalid timestamp") from error
        if sampled_at < earliest:
            raise HTTPException(status_code=422, detail=f"snapshots[{index}] is older than raw retention")
        if sampled_at > latest:
            raise HTTPException(status_code=422, detail=f"snapshots[{index}] is too far in the future")
        for field in array_fields:
            if field in snapshot and not isinstance(snapshot[field], list):
                raise HTTPException(status_code=422, detail=f"snapshots[{index}].{field} must be an array")
        validated.append(snapshot)
    return collector_id.strip(), validated


async def maintain_database(database: TelemetryDatabase, stop: asyncio.Event) -> None:
    while not stop.is_set():
        try:
            if not database.ready:
                await asyncio.to_thread(database.open)
        except Exception as error:
            LOGGER.warning("telemetry database unavailable: %s", error)
        try:
            await asyncio.wait_for(stop.wait(), timeout=2.0)
        except TimeoutError:
            pass


def create_app(
    database_url: str | None = DATABASE_URL,
    database_override: TelemetryDatabase | None = None,
    ingestion_token: str | None = INGESTION_TOKEN,
    initial_snapshot: dict[str, Any] | None = None,
) -> FastAPI:
    database = database_override or (
        TelemetryDatabase(database_url, MIGRATIONS_ROOT) if database_url else None
    )
    latest = LatestSnapshotStore()
    if initial_snapshot:
        latest.update("development-fixture", initial_snapshot)
    stop = asyncio.Event()

    @contextlib.asynccontextmanager
    async def lifespan(_: FastAPI):
        task = asyncio.create_task(maintain_database(database, stop)) if database else None
        yield
        stop.set()
        if task:
            await task
        if database:
            await asyncio.to_thread(database.close)

    app = FastAPI(title="Infrastructure Debugger", docs_url=None, redoc_url=None, lifespan=lifespan)
    app.state.telemetry_database = database
    app.state.latest_snapshot = latest

    @app.get("/api/health")
    def health() -> dict[str, Any]:
        return {
            "status": "ok",
            "database_configured": database is not None,
            "database_ready": bool(database and database.ready),
            **latest.status(),
        }

    @app.get("/api/ready")
    def ready() -> dict[str, str]:
        if database and database.ready:
            return {"status": "ready"}
        raise HTTPException(status_code=503, detail="Telemetry database is unavailable")

    @app.get("/api/snapshot")
    def snapshot() -> dict[str, Any]:
        value = latest.get()
        if value is None:
            raise HTTPException(status_code=503, detail="Waiting for the host collector")
        return value

    @app.post("/v1/infra-snapshots")
    async def ingest_infrastructure(request: Request) -> dict[str, Any]:
        if not ingestion_token:
            raise HTTPException(status_code=503, detail="Infrastructure ingestion is not configured")
        authorization = request.headers.get("authorization", "")
        if not secrets.compare_digest(authorization, f"Bearer {ingestion_token}"):
            raise HTTPException(status_code=401, detail="Invalid ingestion token")
        content_type = request.headers.get("content-type", "").split(";", 1)[0].strip().lower()
        if content_type != "application/json":
            raise HTTPException(status_code=415, detail="Infrastructure ingestion requires application/json")
        content_length = request.headers.get("content-length")
        if content_length:
            try:
                if int(content_length) > MAX_INFRA_PAYLOAD_BYTES:
                    raise HTTPException(status_code=413, detail="Infrastructure payload is too large")
            except ValueError as error:
                raise HTTPException(status_code=400, detail="Invalid Content-Length") from error
        body = await request.body()
        if len(body) > MAX_INFRA_PAYLOAD_BYTES:
            raise HTTPException(status_code=413, detail="Infrastructure payload is too large")
        try:
            payload = json.loads(body)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise HTTPException(status_code=400, detail="Invalid JSON payload") from error
        collector_id, snapshots = validate_infra_envelope(payload)
        if not database or not database.ready:
            raise HTTPException(status_code=503, detail="Telemetry database is unavailable")
        try:
            for item in snapshots:
                await asyncio.to_thread(database.ingest_snapshot, item)
        except Exception as error:
            LOGGER.exception("failed to persist infrastructure snapshots")
            raise HTTPException(status_code=503, detail="Telemetry database is unavailable") from error
        for item in snapshots:
            latest.update(collector_id, item)
        return {
            "accepted": len(snapshots),
            "latest_timestamp_unix_ms": max(int(item["node"]["timestamp_unix_ms"]) for item in snapshots),
        }

    @app.post("/v1/traces")
    async def ingest_traces(request: Request) -> Response:
        if not database or not database.ready:
            raise HTTPException(status_code=503, detail="Telemetry database is unavailable")
        payload = await request.body()
        content_encoding = request.headers.get("content-encoding", "identity").lower()
        if content_encoding == "gzip":
            try:
                payload = gzip.decompress(payload)
            except (gzip.BadGzipFile, EOFError) as error:
                raise HTTPException(status_code=400, detail="Invalid gzip-compressed OTLP trace payload") from error
        elif content_encoding not in {"", "identity"}:
            raise HTTPException(status_code=415, detail=f"Unsupported OTLP content encoding: {content_encoding}")
        try:
            await asyncio.to_thread(database.ingest_otlp_traces, payload)
        except DecodeError as error:
            LOGGER.warning("invalid OTLP protobuf payload: bytes=%d", len(payload))
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
    def deployment_rollups(namespace: str, deployment: str, minutes: int = Query(60, ge=1, le=1440)) -> dict[str, Any]:
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
    parser.add_argument("--database-url", default=DATABASE_URL)
    parser.add_argument("--ingestion-token", default=INGESTION_TOKEN)
    parser.add_argument("--initial-snapshot", type=Path)
    args = parser.parse_args()
    initial = load_snapshot(args.initial_snapshot) if args.initial_snapshot else None
    uvicorn.run(create_app(args.database_url, ingestion_token=args.ingestion_token, initial_snapshot=initial), host=args.host, port=args.port)


if __name__ == "__main__":
    main()
