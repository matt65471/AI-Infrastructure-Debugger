# Infrastructure Dashboard and Telemetry Store

The dashboard receives authenticated host snapshots over HTTP, persists them to
PostgreSQL, and exposes separate Live and Historical modes. It does not call
Linux or Kubernetes itself. Live navigation follows the resource hierarchy:

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
bash scripts/install-host-collector.sh
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

## Preview with a fixture

The representative sample is only for developing the interface:

```bash
source .venv/bin/activate
python3 dashboard/server.py \
  --initial-snapshot dashboard/sample_snapshot.json \
  --database-url ""
```

This fixture option is for local UI work only. The deployed dashboard has no
snapshot hostPath and receives live snapshots through `/v1/infra-snapshots`.
