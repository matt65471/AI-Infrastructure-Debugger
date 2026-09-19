#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
agent_binary="${project_dir}/agent/build/telemetry_agent"
token_file="/etc/ai-infrastructure-debugger/ingest-token"

if [[ ! -x "${agent_binary}" ]]; then
    echo "collector binary not found; run cmake --build agent/build first" >&2
    exit 1
fi
if ! sudo test -s "${token_file}"; then
    echo "ingestion token not found; run scripts/deploy-demo.sh first" >&2
    exit 1
fi

sudo install -d -m 0750 /var/lib/ai-infrastructure-debugger/spool
sudo install -m 0755 "${agent_binary}" /usr/local/bin/telemetry_agent
sudo install -m 0644 \
    "${project_dir}/deploy/telemetry-agent.service" \
    /etc/systemd/system/telemetry-agent.service
sudo systemctl daemon-reload
sudo systemctl enable --now telemetry-agent.service
sudo systemctl --no-pager --full status telemetry-agent.service
