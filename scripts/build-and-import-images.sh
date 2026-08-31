#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image_archive="/tmp/infrastructure-debugger-demo-images.tar"

docker build \
  --file "${project_dir}/demo_app/frontend/Dockerfile" \
  --tag infrastructure-debugger/frontend:v1 \
  "${project_dir}/demo_app"
docker build \
  --file "${project_dir}/demo_app/checkout/Dockerfile" \
  --tag infrastructure-debugger/checkout:v1 \
  "${project_dir}/demo_app"
docker build \
  --file "${project_dir}/demo_app/payment/Dockerfile" \
  --tag infrastructure-debugger/payment:v1 \
  "${project_dir}/demo_app"

docker save \
  --output "${image_archive}" \
  infrastructure-debugger/frontend:v1 \
  infrastructure-debugger/checkout:v1 \
  infrastructure-debugger/payment:v1

sudo k3s ctr --namespace k8s.io images import "${image_archive}"
sudo k3s ctr --namespace k8s.io images list | grep infrastructure-debugger
