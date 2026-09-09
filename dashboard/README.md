# Infrastructure Dashboard and Telemetry Store

The dashboard shows the latest combined telemetry snapshot plus PostgreSQL-backed
one-minute history. It does not call Linux or Kubernetes itself. Navigation
follows the resource hierarchy:

```text
Node -> Deployment -> Pod -> Container -> Linux process
```

## Run with live VM telemetry

Build the agent once:

```bash
cmake -S agent -B agent/build
cmake --build agent/build
```

Build/import the application and dashboard images, then deploy PostgreSQL,
dashboard, OpenTelemetry Collector, and the demo application:

```bash
bash scripts/build-and-import-images.sh
bash scripts/deploy-demo.sh
```

Start the host collector:

```bash
sudo ./agent/build/telemetry_agent \
  --json-file /var/lib/ai-infrastructure-debugger/snapshot.json
```

Open `http://<vm-ip-address>:30081` on the Mac. PostgreSQL is not exposed
outside the cluster. Check the storage and workloads with:

```bash
sudo k3s kubectl get statefulset,pods,pvc,pv -n infrastructure-demo
sudo k3s kubectl get cronjobs -n infrastructure-demo
```

Inspect database and maintenance logs without exposing PostgreSQL:

```bash
sudo k3s kubectl exec -n infrastructure-demo postgres-0 -- \
  psql -U telemetry -d telemetry -c '\dt telemetry.*'
sudo k3s kubectl logs -n infrastructure-demo deployment/telemetry-dashboard
sudo k3s kubectl get jobs -n infrastructure-demo
```

Find the VM address:

```bash
hostname -I
```

The rollup job runs once per minute. Raw spans and infrastructure partitions are
kept for seven days; one-minute rollups and Kubernetes Events are kept for 30
days. The PVC and PV use `Retain`, but they are not a backup of the VM disk.
The deployment script creates the database Secret only when it is absent, so
rerunning the script does not rotate credentials away from an existing retained
database.

## Preview without the collector

The representative sample is only for developing the interface:

```bash
source .venv/bin/activate
python3 dashboard/server.py \
  --snapshot-file dashboard/sample_snapshot.json \
  --database-url ""
```

The header says `Live telemetry` because the dashboard is successfully polling
the selected file; the sample timestamp and `sample_snapshot.json` path make it
clear that this is not current VM data.
