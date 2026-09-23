import json
import subprocess
import unittest
from unittest.mock import patch

from fault_injection.runner import (
    Kubernetes,
    NAMESPACE,
    TrafficGenerator,
    build_parser,
)


class KubernetesSafetyTest(unittest.TestCase):
    def test_rejects_any_namespace_other_than_demo(self) -> None:
        with self.assertRaises(ValueError):
            Kubernetes(["kubectl"], "default")

    @patch("fault_injection.runner.subprocess.run")
    def test_requires_a_single_ready_target_pod(self, run) -> None:
        deployment = {
            "spec": {"replicas": 1},
            "status": {"readyReplicas": 1, "availableReplicas": 1},
        }
        pods = {
            "items": [{
                "metadata": {"name": "payment-abc"},
                "spec": {"containers": [{"name": "payment"}]},
                "status": {
                    "phase": "Running",
                    "containerStatuses": [{"name": "payment", "ready": True}],
                },
            }]
        }
        run.side_effect = [
            subprocess.CompletedProcess([], 0, json.dumps(deployment), ""),
            subprocess.CompletedProcess([], 0, json.dumps(pods), ""),
        ]
        client = Kubernetes(["kubectl"], NAMESPACE)
        client.require_ready_deployment("payment")
        self.assertEqual(client.ready_pod("payment"), ("payment-abc", "payment"))


class TrafficSummaryTest(unittest.TestCase):
    def test_empty_summary_is_stable(self) -> None:
        traffic = TrafficGenerator("http://127.0.0.1:1")
        self.assertEqual(traffic.summary(), {})
        self.assertFalse(traffic.recovered())

    def test_parser_supports_healthy_observations(self) -> None:
        args = build_parser().parse_args(["healthy"])
        self.assertEqual(args.fault, "healthy")


if __name__ == "__main__":
    unittest.main()
