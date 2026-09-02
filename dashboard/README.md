# Infrastructure Dashboard

The dashboard is a read-only view of the latest combined telemetry snapshot.
It does not call Linux or Kubernetes itself. Navigation follows the resource
hierarchy:

```text
Node -> Deployment -> Pod -> Container -> Linux process
```

## Run with live VM telemetry

Build the agent once:

```bash
cmake -S agent -B agent/build
cmake --build agent/build
```

Start the collector in the first VM terminal:

```bash
sudo ./agent/build/telemetry_agent \
  --json-file /tmp/ai-infrastructure-debugger-snapshot.json
```

Start the dashboard in a second VM terminal:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r demo_app/requirements.txt
python3 dashboard/server.py --host 0.0.0.0 --port 8080
```

Find the VM address:

```bash
hostname -I
```

Open `http://<vm-ip-address>:8080` on the Mac. Binding to `0.0.0.0` makes the
dashboard reachable from the VM network, so only use it on a trusted network.
Use the default `127.0.0.1` binding if the browser runs inside the VM.

## Preview without the collector

The representative sample is only for developing the interface:

```bash
source .venv/bin/activate
python3 dashboard/server.py \
  --snapshot-file dashboard/sample_snapshot.json
```

The header says `Live telemetry` because the dashboard is successfully polling
the selected file; the sample timestamp and `sample_snapshot.json` path make it
clear that this is not current VM data.
