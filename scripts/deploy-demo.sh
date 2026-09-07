#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
namespace="infrastructure-demo"

sudo k3s kubectl apply -f "${project_dir}/kubernetes/demo-app.yaml"
sudo k3s kubectl rollout status deployment/otel-collector --namespace "${namespace}" --timeout=120s
sudo k3s kubectl rollout status deployment/payment --namespace "${namespace}" --timeout=120s
sudo k3s kubectl rollout status deployment/checkout --namespace "${namespace}" --timeout=120s
sudo k3s kubectl rollout status deployment/frontend --namespace "${namespace}" --timeout=120s
sudo k3s kubectl get pods,services --namespace "${namespace}" --output wide
