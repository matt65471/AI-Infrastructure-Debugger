#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
namespace="infrastructure-demo"

sudo k3s kubectl apply -f "${project_dir}/kubernetes/namespace.yaml"

if ! sudo k3s kubectl get secret telemetry-postgres --namespace "${namespace}" >/dev/null 2>&1; then
    database_password="$(openssl rand -hex 24)"
    sudo k3s kubectl create secret generic telemetry-postgres \
        --namespace "${namespace}" \
        --from-literal=username=telemetry \
        --from-literal=password="${database_password}" \
        --from-literal=database=telemetry \
        --from-literal=database-url="postgresql://telemetry:${database_password}@postgres:5432/telemetry"
fi

sudo install -d -m 0755 /var/lib/ai-infrastructure-debugger
sudo k3s kubectl apply -f "${project_dir}/kubernetes/postgres.yaml"
sudo k3s kubectl rollout status statefulset/postgres --namespace "${namespace}" --timeout=180s
sudo k3s kubectl apply -f "${project_dir}/kubernetes/dashboard.yaml"
sudo k3s kubectl apply -f "${project_dir}/kubernetes/demo-app.yaml"
sudo k3s kubectl rollout restart deployment/telemetry-dashboard --namespace "${namespace}"
sudo k3s kubectl rollout restart deployment/otel-collector --namespace "${namespace}"
sudo k3s kubectl rollout restart deployment/payment --namespace "${namespace}"
sudo k3s kubectl rollout restart deployment/checkout --namespace "${namespace}"
sudo k3s kubectl rollout restart deployment/frontend --namespace "${namespace}"
sudo k3s kubectl rollout status deployment/telemetry-dashboard --namespace "${namespace}" --timeout=180s
sudo k3s kubectl rollout status deployment/otel-collector --namespace "${namespace}" --timeout=120s
sudo k3s kubectl rollout status deployment/payment --namespace "${namespace}" --timeout=120s
sudo k3s kubectl rollout status deployment/checkout --namespace "${namespace}" --timeout=120s
sudo k3s kubectl rollout status deployment/frontend --namespace "${namespace}" --timeout=120s
sudo k3s kubectl get pods,services --namespace "${namespace}" --output wide
