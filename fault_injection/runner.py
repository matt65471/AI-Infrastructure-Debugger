#!/usr/bin/env python3
"""Run a labeled, time-bounded CPU saturation experiment."""

from __future__ import annotations

import argparse
import base64
import json
import shlex
import subprocess
import threading
import time
import urllib.error
import urllib.request
import uuid
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any


NAMESPACE = "infrastructure-demo"
TOKEN_SECRET = "telemetry-ingestion"
CPU_PROGRAM = """\
import sys, time
deadline = time.monotonic() + float(sys.argv[1])
value = 1
while time.monotonic() < deadline:
    for _ in range(10000):
        value = (value * 3 + 1) % 1000000007
print(value)
"""


def utc_now() -> datetime:
    return datetime.now(UTC)


class ExperimentApi:
    def __init__(self, base_url: str, token: str):
        self.base_url = base_url.rstrip("/")
        self.token = token

    def request(
        self, method: str, path: str, payload: dict[str, Any] | None = None
    ) -> dict[str, Any]:
        body = json.dumps(payload).encode() if payload is not None else None
        headers = {"accept": "application/json"}
        if payload is not None:
            headers["content-type"] = "application/json"
        if self.token:
            headers["authorization"] = f"Bearer {self.token}"
        request = urllib.request.Request(
            f"{self.base_url}{path}", data=body, headers=headers, method=method
        )
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                return json.loads(response.read())
        except urllib.error.HTTPError as error:
            detail = error.read().decode(errors="replace")
            raise RuntimeError(f"dashboard returned HTTP {error.code}: {detail}") from error
        except urllib.error.URLError as error:
            raise RuntimeError(f"dashboard is unreachable: {error.reason}") from error

    def create(self, payload: dict[str, Any]) -> dict[str, Any]:
        return self.request("POST", "/v1/fault-experiments", payload)

    def update(
        self,
        experiment_id: uuid.UUID,
        status: str,
        *,
        error_message: str | None = None,
        traffic_summary: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "status": status,
            "observed_at": utc_now().isoformat(),
        }
        if error_message is not None:
            payload["error_message"] = error_message[:4000]
        if traffic_summary is not None:
            payload["traffic_summary"] = traffic_summary
        return self.request(
            "PATCH", f"/v1/fault-experiments/{experiment_id}", payload
        )


class Kubernetes:
    def __init__(self, command: list[str], namespace: str):
        if namespace != NAMESPACE:
            raise ValueError(f"fault injection is restricted to {NAMESPACE}")
        if not command:
            raise ValueError("kubectl command cannot be empty")
        self.command = command
        self.namespace = namespace

    def run(self, *arguments: str, timeout: int = 30) -> str:
        result = subprocess.run(
            [*self.command, *arguments],
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        if result.returncode:
            detail = result.stderr.strip() or result.stdout.strip()
            raise RuntimeError(f"kubectl failed: {detail}")
        return result.stdout

    def get_json(self, *arguments: str) -> dict[str, Any]:
        return json.loads(self.run(*arguments, "--output", "json"))

    def require_ready_deployment(self, deployment: str) -> None:
        value = self.get_json(
            "get", "deployment", deployment, "--namespace", self.namespace
        )
        desired = int(value.get("spec", {}).get("replicas", 1))
        status = value.get("status", {})
        ready = int(status.get("readyReplicas", 0))
        available = int(status.get("availableReplicas", 0))
        if desired < 1 or ready != desired or available != desired:
            raise RuntimeError(
                f"deployment/{deployment} is not healthy "
                f"(desired={desired}, ready={ready}, available={available})"
            )

    def ready_pod(self, deployment: str) -> tuple[str, str]:
        pods = self.get_json(
            "get",
            "pods",
            "--selector",
            f"app={deployment}",
            "--namespace",
            self.namespace,
        ).get("items", [])
        matches: list[tuple[str, str]] = []
        for pod in pods:
            statuses = pod.get("status", {}).get("containerStatuses", [])
            if pod.get("status", {}).get("phase") != "Running":
                continue
            if not statuses or not all(item.get("ready") for item in statuses):
                continue
            containers = pod.get("spec", {}).get("containers", [])
            names = [item.get("name") for item in containers]
            if deployment in names:
                matches.append((pod["metadata"]["name"], deployment))
        if len(matches) != 1:
            raise RuntimeError(
                f"expected exactly one ready {deployment} pod, found {len(matches)}"
            )
        return matches[0]

    def ingestion_token(self) -> str:
        secret = self.get_json(
            "get", "secret", TOKEN_SECRET, "--namespace", self.namespace
        )
        encoded = secret.get("data", {}).get("token")
        if not encoded:
            raise RuntimeError(f"secret/{TOKEN_SECRET} does not contain token")
        return base64.b64decode(encoded, validate=True).decode()

    def saturate_cpu(self, pod: str, container: str, duration: int) -> None:
        self.run(
            "exec",
            pod,
            "--namespace",
            self.namespace,
            "--container",
            container,
            "--",
            "python3",
            "-c",
            CPU_PROGRAM,
            str(duration),
            timeout=duration + 20,
        )


class TrafficGenerator:
    def __init__(self, url: str, interval: float = 1.0):
        self.url = url
        self.interval = interval
        self.phase = "baseline"
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        self.thread: threading.Thread | None = None
        self.values: dict[str, dict[str, float | int]] = {}
        self.consecutive_successes = 0

    def start(self) -> None:
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def set_phase(self, phase: str) -> None:
        with self.lock:
            self.phase = phase
            self.consecutive_successes = 0

    def stop(self) -> None:
        self.stop_event.set()
        if self.thread:
            self.thread.join(timeout=5)

    def _run(self) -> None:
        while not self.stop_event.is_set():
            started = time.monotonic()
            succeeded = False
            try:
                with urllib.request.urlopen(self.url, timeout=5) as response:
                    response.read()
                    succeeded = 200 <= response.status < 300
            except (OSError, urllib.error.URLError):
                succeeded = False
            elapsed_ms = (time.monotonic() - started) * 1000
            with self.lock:
                summary = self.values.setdefault(
                    self.phase,
                    {"requests": 0, "successes": 0, "failures": 0, "latency_ms_sum": 0.0},
                )
                summary["requests"] += 1
                summary["successes" if succeeded else "failures"] += 1
                summary["latency_ms_sum"] += elapsed_ms
                self.consecutive_successes = (
                    self.consecutive_successes + 1 if succeeded else 0
                )
            self.stop_event.wait(self.interval)

    def summary(self) -> dict[str, Any]:
        with self.lock:
            result: dict[str, Any] = {}
            for phase, values in self.values.items():
                requests = int(values["requests"])
                result[phase] = {
                    "requests": requests,
                    "successes": int(values["successes"]),
                    "failures": int(values["failures"]),
                    "average_latency_ms": round(
                        float(values["latency_ms_sum"]) / requests, 3
                    ) if requests else None,
                }
            return result

    def recovered(self, minimum_successes: int = 3) -> bool:
        with self.lock:
            return self.consecutive_successes >= minimum_successes


def run_experiment(args: argparse.Namespace) -> uuid.UUID:
    if args.confirm != NAMESPACE:
        raise RuntimeError(f"pass --confirm {NAMESPACE} to authorize the fault")
    kubernetes = Kubernetes(shlex.split(args.kubectl), NAMESPACE)
    kubernetes.require_ready_deployment(args.target)
    pod, container = kubernetes.ready_pod(args.target)
    token = (
        Path(args.token_file).read_text(encoding="utf-8").strip()
        if args.token_file
        else kubernetes.ingestion_token()
    )
    api = ExperimentApi(args.dashboard_url, token)
    api.request("GET", "/api/ready")

    experiment_id = uuid.uuid4()
    baseline_started_at = utc_now()
    expires_at = baseline_started_at + timedelta(
        seconds=args.baseline + args.duration + args.recovery + 30
    )
    api.create(
        {
            "id": str(experiment_id),
            "fault_type": "cpu_saturation",
            "namespace_name": NAMESPACE,
            "target_kind": "deployment",
            "target_name": args.target,
            "parameters": {
                "duration_seconds": args.duration,
                "pod": pod,
                "container": container,
            },
            "baseline_started_at": baseline_started_at.isoformat(),
            "expires_at": expires_at.isoformat(),
        }
    )

    traffic = TrafficGenerator(args.traffic_url, args.traffic_interval)
    state = "baseline"
    traffic.start()
    try:
        print(f"experiment_id={experiment_id} phase=baseline seconds={args.baseline}")
        time.sleep(args.baseline)
        if not traffic.recovered():
            raise RuntimeError(
                "baseline verification failed: application traffic is unhealthy"
            )
        state = "injecting"
        traffic.set_phase(state)
        api.update(experiment_id, state)
        print(
            f"experiment_id={experiment_id} phase=injecting "
            f"target={pod}/{container} seconds={args.duration}"
        )
        kubernetes.saturate_cpu(pod, container, args.duration)

        state = "recovering"
        traffic.set_phase(state)
        api.update(experiment_id, state)
        print(f"experiment_id={experiment_id} phase=recovering seconds={args.recovery}")
        time.sleep(args.recovery)
        kubernetes.require_ready_deployment(args.target)
        if not traffic.recovered():
            summary = traffic.summary()
            api.update(
                experiment_id,
                "recovery_failed",
                error_message="traffic did not produce three consecutive successful requests",
                traffic_summary=summary,
            )
            raise RuntimeError("recovery verification failed: application traffic is unhealthy")
        summary = traffic.summary()
        api.update(experiment_id, "completed", traffic_summary=summary)
        print(f"experiment_id={experiment_id} phase=completed traffic={json.dumps(summary)}")
        return experiment_id
    except Exception as error:
        terminal_status = "recovery_failed" if state == "recovering" else "failed"
        try:
            api.update(
                experiment_id,
                terminal_status,
                error_message=str(error),
                traffic_summary=traffic.summary(),
            )
        except Exception:
            pass
        raise
    finally:
        traffic.stop()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "fault", nargs="?", choices=("cpu-saturation",), default="cpu-saturation"
    )
    parser.add_argument(
        "--target", choices=("payment", "checkout", "frontend"), default="payment"
    )
    parser.add_argument(
        "--duration", type=int, default=30,
        choices=range(5, 121), metavar="5..120",
    )
    parser.add_argument(
        "--baseline", type=int, default=30,
        choices=range(5, 121), metavar="5..120",
    )
    parser.add_argument(
        "--recovery", type=int, default=60,
        choices=range(5, 181), metavar="5..180",
    )
    parser.add_argument("--traffic-url", default="http://127.0.0.1:30080/api/order")
    parser.add_argument("--traffic-interval", type=float, default=1.0)
    parser.add_argument("--dashboard-url", default="http://127.0.0.1:30081")
    parser.add_argument("--token-file")
    parser.add_argument("--kubectl", default="sudo k3s kubectl")
    parser.add_argument("--confirm", help=f"must be exactly {NAMESPACE}")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    if args.traffic_interval <= 0:
        raise SystemExit("--traffic-interval must be greater than zero")
    try:
        run_experiment(args)
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
