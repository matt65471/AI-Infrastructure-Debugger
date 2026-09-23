# AI Infrastructure Debugger

This project is an incremental systems project for learning how Linux,
infrastructure, observability, and eventually Kubernetes/root-cause analysis fit
together.

The long-term goal is to build a debugger that can monitor distributed
applications, detect failures, and identify likely root causes from telemetry
and service dependencies.

The project currently contains a C++ Linux telemetry agent, a small Kubernetes
application used to generate service-to-service behavior, OpenTelemetry tracing,
and PostgreSQL-backed history. A dashboard running in k3s presents live state
and one-minute application/infrastructure rollups as a drill-down hierarchy.

## Current Phase

The Linux agent collects node, process, cgroup, container, pod, and Deployment
telemetry once per second. The included demo application runs on a single-node
k3s cluster and is ready to be used as the target for fault-injection tests.

Current metrics:

- CPU utilization from `/proc/stat` plus the online logical CPU count
- memory/swap capacity and utilization from `/proc/meminfo`
- network receive/transmit byte rates from `/proc/net/dev`
- TCP/IP counters from `/proc/net/snmp` and `/proc/net/netstat`
- disk I/O counters from `/proc/diskstats`
- load averages and runnable-process counts from `/proc/loadavg`
- CPU, memory, and I/O pressure stall information from `/proc/pressure`
- top process CPU and memory metrics from `/proc/[pid]`
- raw Kubernetes container cgroup v2 counters, CPU quotas, and memory events
- calculated per-container CPU, throttling, memory-pressure, and OOM metrics
- Kubernetes identity, lifecycle state, QoS, and resource requests/limits
- pod-level resource and lifecycle aggregates
- Deployment desired/ready/available replica state and pod resource aggregates
- the 50 most recent Kubernetes Events

Not included yet:

- Service dependency attribution and per-container network telemetry
- gRPC exporting
- machine learning
- eBPF

## Project Layout

```text
agent/
├── CMakeLists.txt
├── include/
│   ├── cpu_collector.h
│   ├── cgroup_collector.h
│   ├── container_metric_calculator.h
│   ├── deployment_metric_aggregator.h
│   ├── disk_collector.h
│   ├── kubernetes_metadata_collector.h
│   ├── mem_collector.h
│   ├── network_collector.h
│   ├── pod_metric_aggregator.h
│   ├── pressure_collector.h
│   ├── process_collector.h
│   ├── tcp_collector.h
│   └── telemetry_collector.h
└── src/
    ├── container_metric_calculator.cpp
    ├── deployment_metric_aggregator.cpp
    ├── kubernetes_metadata_collector.cpp
    ├── pod_metric_aggregator.cpp
    ├── linux/
    │   ├── cgroup_collector.cpp
    │   ├── cpu_collector.cpp
    │   ├── disk_collector.cpp
    │   ├── mem_collector.cpp
    │   ├── network_collector.cpp
    │   ├── process_collector.cpp
    │   ├── pressure_collector.cpp
    │   └── tcp_collector.cpp
    ├── main.cpp
    └── telemetry_collector.cpp

test_workloads/
├── README.md
└── workload.py

demo_app/
├── frontend/
├── checkout/
├── payment/
├── requirements.txt
└── README.md

kubernetes/
├── namespace.yaml
├── postgres.yaml
├── dashboard.yaml
└── demo-app.yaml

dashboard/
├── Dockerfile
├── database.py
├── features.py
├── maintenance.py
├── migrations/
├── server.py
├── sample_snapshot.json
└── static/
    ├── app.js
    ├── index.html
    └── styles.css
```

## How The Agent Works

The collectors each own one Linux data source:

```text
CpuCollector       -> /proc/stat
CgroupCollector    -> /proc/[pid]/cgroup, /sys/fs/cgroup
MemoryCollector    -> /proc/meminfo
NetworkCollector   -> /proc/net/dev
TcpCollector       -> /proc/net/snmp, /proc/net/netstat
DiskCollector      -> /proc/diskstats
ProcessCollector   -> /proc/[pid]/stat, /proc/[pid]/status, /proc/[pid]/io
PressureCollector  -> /proc/loadavg, /proc/pressure/{cpu,memory,io}
TelemetryCollector -> coordinates the collectors
```

The cgroup collector detects cgroup v2, discovers host processes in `kubepods`
cgroups, and reads raw `cpu.stat`, `cpu.max`, `memory.current`, `memory.max`,
`memory.events`, and `cgroup.procs` values. The container metric calculator
matches consecutive samples by full container ID and calculates CPU usage,
throttling and memory-event deltas, memory utilization, and the effective CPU
quota in cores. The pressure collector reports whether tasks are stalled by
CPU, memory, or I/O contention rather than merely reporting utilization.

The Kubernetes metadata collector refreshes every five seconds and uses the
full container ID to attach node, namespace, pod UID/name, container
name/image, pod conditions, QoS class, current and previous termination state,
restart count, configured requests/limits, and owning workload. It also reads
Deployment replica status and recent Events. For this VM stage it invokes the
local k3s `kubectl`; a future long-running service can use the Kubernetes API
directly with a service account.

The pod metric aggregator groups Kubernetes-matched containers by pod UID. It
sums CPU, memory, throttling, OOM, and restart values, combines container IDs
and host PIDs, and requires every included container to be ready before the pod
aggregate is marked ready. The existing node metrics continue to come directly
from Linux host counters; they are not calculated by summing pods, which would
omit Kubernetes and host overhead.

The Deployment aggregator combines pod measurements with Kubernetes desired,
ready, available, unavailable, updated, and generation state. This makes it
possible to distinguish a busy healthy workload from one whose pods are
missing, restarting, throttled, or being OOM-killed.

`main.cpp` does not parse Linux files directly. It creates a
`TelemetryCollector`, calls `collect()` once per second, and prints the combined
snapshot.

In the deployed system, the agent sends each snapshot to the authenticated
`POST /v1/infra-snapshots` endpoint. The backend persists it to PostgreSQL
before exposing it as Live state. A bounded disk spool protects delivery while
the API or database is unavailable. `--json` writes snapshots to standard
output for diagnostics without participating in ingestion.

Example output:

```text
timestamp_unix_ms=1788217200000 node=ubuntu-vm cpu_usage_percent=3.20 memory_usage_percent=41.75 memory_available_kb=4045320 network_rx_bytes_per_second=1204 network_tx_bytes_per_second=884 tcp_retransmits_per_second=0 disk_read_bytes_per_second=0 disk_write_bytes_per_second=4096 load_average_1m=0.20 cpu_pressure={some={avg10=0.00,...},full=na} memory_pressure={some={...},full={...}} io_pressure={some={...},full={...}} top_cpu=[1234:payment:12.40] top_memory=[1234:payment:524288] cgroup_v2=true kubernetes_metadata=available containers=[...] pods=[...] deployments=[...] kubernetes_events=[...]
```

Each `TelemetrySnapshot` contains one explicit `NodeMetric` plus separate
process and cgroup collections. The node record adds the collection time in
Unix milliseconds and the Linux hostname to the existing host-wide CPU,
memory, network, TCP, and disk metrics. The hostname is local Linux identity;
each matched container also reports the node name assigned by Kubernetes.

## Run The Complete System

This project must run on Linux because the host collector reads Linux-specific
files under `/proc` and `/sys/fs/cgroup`. The expected development environment
is an Ubuntu VM running k3s.

### First-time VM setup

Install the build and container tools:

```bash
sudo apt update
sudo apt install -y build-essential cmake curl docker.io git libcurl4-openssl-dev openssl python3 python3-venv
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
```

Log out and back in after adding yourself to the Docker group, then verify:

```bash
docker version
```

Install k3s once:

```bash
curl -sfL https://get.k3s.io | sh -
sudo k3s kubectl get nodes
```

### Start or redeploy everything

Run these commands from the repository on the VM whenever application,
dashboard, Collector, or Kubernetes files have changed:

Before rebuilding, make sure the VM checkout contains the latest source. A
Docker rebuild cannot include fixes that only exist in a different checkout.
If the repository is synchronized through Git, update it before continuing:

```bash
cd ~/AI-Infrastructure-Debugger
git pull --ff-only
```

```bash
cd ~/AI-Infrastructure-Debugger

sudo systemctl start docker
sudo systemctl start k3s
sudo k3s kubectl wait --for=condition=Ready node --all --timeout=120s

cmake -S agent -B agent/build -DBUILD_TESTING=ON
cmake --build agent/build
ctest --test-dir agent/build --output-on-failure

bash scripts/build-and-import-images.sh
bash scripts/deploy-demo.sh
bash scripts/install-host-collector.sh
```

If the old `--json-file` collector is still running in another terminal, stop
that process with `Ctrl+C`; the systemd service is now the only deployed
collector.

The deployment script creates or updates PostgreSQL, the dashboard,
OpenTelemetry Collector, and the frontend, checkout, and payment services. It
preserves the existing PostgreSQL Secret and retained volume.

Verify that the workloads are ready:

```bash
sudo k3s kubectl get pods,statefulset,pvc,services,cronjobs \
  -n infrastructure-demo
```

### Verify the host collector

The installation script runs the collector as a systemd service. It posts each
snapshot to the authenticated ingestion API and queues unsent snapshots under
`/var/lib/ai-infrastructure-debugger/spool`:

```bash
sudo systemctl status telemetry-agent --no-pager
sudo journalctl -u telemetry-agent -f
```

Inspect the retry queue and backend ingestion responses with:

```bash
sudo find /var/lib/ai-infrastructure-debugger/spool -maxdepth 1 -name '*.json' | wc -l
sudo k3s kubectl logs -n infrastructure-demo deployment/telemetry-dashboard --since=5m | grep '/v1/infra-snapshots'
```

The queue is normally empty or briefly contains the in-flight sample. A growing
queue means the API, token, network, or PostgreSQL is unavailable.

### Open and exercise the system

Find the VM address:

```bash
hostname -I
```

From the Mac, open:

```text
Demo application: http://<vm-ip-address>:30080
Dashboard:        http://<vm-ip-address>:30081
```

Generate a single distributed request:

```bash
curl http://<vm-ip-address>:30080/api/order
```

To make the one-minute charts easier to see, generate traffic across a minute
boundary:

```bash
for i in {1..40}; do
  curl -s http://<vm-ip-address>:30080/api/order >/dev/null
  sleep 2
done
```

Use the dashboard header to switch between **Live** and **Historical**. Live
updates every second and shows the current hierarchy. Historical offers 1-hour,
6-hour, and 24-hour PostgreSQL-backed ranges. Historical charts contain
one-minute buckets, so wait through a minute boundary and allow up to another
15 seconds for the dashboard poll.

### Verify telemetry and rollups

Application traces should be accepted with `200 OK`:

```bash
sudo k3s kubectl logs -n infrastructure-demo \
  deployment/telemetry-dashboard --since=5m | grep '/v1/traces'
```

Check the Collector for export errors:

```bash
sudo k3s kubectl logs -n infrastructure-demo \
  deployment/otel-collector --since=5m
```

Inspect stored spans:

```bash
sudo k3s kubectl exec -n infrastructure-demo postgres-0 -- \
  psql -U telemetry -d telemetry -c \
  "SELECT count(*) AS spans, max(started_at) AS latest_span FROM telemetry.spans;"
```

Inspect the latest one-minute summaries:

```bash
sudo k3s kubectl exec -n infrastructure-demo postgres-0 -- \
  psql -U telemetry -d telemetry -c \
  "SELECT bucket, deployment_name, request_count, error_count, p95_latency_ms
   FROM telemetry.service_rollups_1m
   WHERE http_route = ''
   ORDER BY bucket DESC, deployment_name
   LIMIT 20;"
```

If needed, recompute the five most recently completed minutes immediately:

```bash
sudo k3s kubectl exec -n infrastructure-demo \
  deployment/telemetry-dashboard -- python3 maintenance.py rollup
```

See [`dashboard/README.md`](dashboard/README.md) for additional storage and
dashboard details.

### Restart after a VM reboot

The Kubernetes workloads restart automatically with k3s. Normally, only these
commands are needed after a reboot:

```bash
sudo systemctl start docker
sudo systemctl start k3s
sudo k3s kubectl get pods -n infrastructure-demo
sudo systemctl status telemetry-agent --no-pager
```

Rebuild and redeploy only when the source code, images, or Kubernetes manifests
have changed.

### Troubleshooting a frozen or empty dashboard

Check the collector service and ingestion health:

```bash
sudo systemctl status telemetry-agent --no-pager
sudo journalctl -u telemetry-agent --since=-5m
curl -s http://127.0.0.1:30081/api/health | python3 -m json.tool
```

Check whether delivery is backing up:

```bash
sudo find /var/lib/ai-infrastructure-debugger/spool -maxdepth 1 -name '*.json' -ls
```

If the queue grows, verify PostgreSQL and the dashboard are Ready, then restart
the service after correcting the failure. Queued snapshots drain oldest-first.

If node history updates but Application health is empty, check that trace
requests return `200` rather than `400`, then confirm spans and rollups with the
queries above. Browser `304 Not Modified` responses for `app.js` and
`styles.css` are normal cache validation and are not errors.

With k3s and the demo application running, container calculation, Kubernetes
identity, pod/Deployment aggregation, and Events are working when the output
contains:

```text
cgroup_v2=true kubernetes_metadata=available containers=[...] pods=[...] deployments=[...] kubernetes_events=[...]
```

Each entry reports a shortened container ID, host cgroup path, CPU percentage,
cumulative CPU time, effective cgroup CPU quota, throttling deltas, current and
maximum memory, memory-pressure/OOM deltas, and host PIDs. Kubernetes enrichment
adds configured CPU/memory requests and limits; keeping those distinct from
cgroup enforcement helps reveal configuration/runtime mismatches. `100%` CPU
means one fully used core and a multi-core container can exceed `100%`. A newly
seen container reports `na` for CPU until it has two samples; unlimited memory
reports `na` for memory percentage. Run the agent with `sudo` on the k3s VM so
it can read all host processes and use k3s cluster credentials. Always include
the HTTP ingestion configuration:

```bash
sudo systemctl cat telemetry-agent
```

When Kubernetes identity matches, each container entry also includes:

```text
kubernetes={node=ubuntu-vm,namespace=infrastructure-demo,pod=payment-...,pod_uid=...,container=payment,image=infrastructure-debugger/payment:v1,phase=Running,qos=Burstable,pod_ready=true,scheduled=true,initialized=true,ready=true,restarts=0,state=Running,reason=,exit_code=0,last_reason=,last_exit_code=0,cpu_request_cores=0.1,cpu_limit_cores=0.5,memory_request_bytes=67108864,memory_limit_bytes=134217728,workload=Deployment/payment}
```

Pod sandbox containers and containers that have not appeared in the latest
Kubernetes metadata refresh remain visible with `kubernetes=unmatched`.
Unmatched containers are not included in pod aggregates because they have no
reliable pod UID.

A pod aggregate resembles:

```text
{node=ubuntu-vm,namespace=infrastructure-demo,pod=payment-...,pod_uid=...,workload=Deployment/payment,phase=Running,qos=Burstable,pod_ready=true,ready=true,container_count=1,container_names=[payment],container_ids=[91ab2345cdef],cpu_usage_percent=12.40,cpu_limit_cores=0.50,memory_current_bytes=73400320,memory_max=134217728,memory_usage_percent=54.69,oom_kill_delta=0,restarts=0,cpu_request_cores=0.10,configured_cpu_limit_cores=0.50,lifecycle_reasons=[],pids=[1488]}
```

Before injecting faults, capture a healthy baseline for at least 30 seconds and
confirm that `cgroup_v2=true`, `kubernetes_metadata=available`, all expected
Deployments have matching desired/ready replicas, and the expected pods appear.
During injection, watch these primary correlations:

```text
CPU saturation   -> cpu_usage_percent + cpu_pressure.some.avg10 + throttled_usec_delta
Memory pressure  -> memory_pressure.full.avg10 + memory_high_delta + oom_kill_delta
Pod crash        -> state/reason + last_reason/last_exit_code + restarts + Events
Rollout failure  -> desired/ready/unavailable replicas + generation mismatch + Events
Network fault    -> TCP retransmit/reset/timeout deltas + application behavior
```

Network counters are currently node-wide. Reliable pod/service network
attribution remains a later eBPF or connection-tracing phase.

## First Controlled Fault Experiment

The first automated scenario saturates CPU inside one demo application
container. It is restricted to the `infrastructure-demo` namespace, requires
an explicit confirmation argument, and the injected Python process enforces
its own deadline. The runner generates order traffic during a healthy
baseline, the injection, and recovery. It records all phase timestamps and the
traffic summary in `telemetry.fault_experiments`.

Rebuild and deploy the dashboard image so the experiment migrations and API
are available, then run on
the k3s VM from the repository root:

```bash
bash scripts/build-and-import-images.sh
bash scripts/deploy-demo.sh
python3 -m fault_injection.runner cpu-saturation \
  --target payment \
  --baseline 30 \
  --duration 30 \
  --recovery 60 \
  --confirm infrastructure-demo
```

The runner refuses to begin unless the target Deployment has exactly one ready
pod, the dashboard database is ready, and the ingestion Secret is available.
An experiment is marked `completed` only when the Deployment remains ready and
application traffic produces at least three consecutive successful requests
during recovery. Inspect the resulting labels with:

```bash
sudo k3s kubectl exec -n infrastructure-demo postgres-0 -- \
  sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -c \
  "SELECT id, fault_type, target_name, status, baseline_started_at, active_started_at, active_ended_at, recovery_completed_at FROM telemetry.fault_experiments ORDER BY created_at DESC;"'
```

Only one experiment may be active at a time. A healthy control uses the same
traffic and timing without applying a fault:

```bash
python3 -m fault_injection.runner healthy \
  --baseline 30 \
  --duration 30 \
  --recovery 60 \
  --confirm infrastructure-demo
```

The `telemetry-features` CronJob converts completed experiments into versioned
five-second rows. `fault_experiments` stores labels,
`experiment_feature_builds` records whether feature generation completed, and
`experiment_feature_buckets` stores one row per experiment, time bucket, and
node/Deployment/pod/container resource. Raw telemetry remains the source of
truth. Feature generation waits 30 seconds after recovery for telemetry to
arrive and retains missing measurements as `NULL`.

Inspect recent builds and their row counts:

```bash
sudo k3s kubectl exec -n infrastructure-demo postgres-0 -- \
  sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -c \
  "SELECT experiment_id, feature_version, status, row_count, error_message FROM telemetry.experiment_feature_builds ORDER BY started_at DESC;"'
```

Force a manual rebuild when changing feature version 1 during development:

```bash
sudo k3s kubectl exec -n infrastructure-demo \
  deployment/telemetry-dashboard -- \
  python3 maintenance.py features --experiment-id <experiment-uuid> --force
```

## Test Workloads

Run the telemetry agent in one terminal, then run a workload in another.

CPU load:

```bash
python3 test_workloads/workload.py cpu --workers 1 --duration 30
```

Memory load:

```bash
python3 test_workloads/workload.py memory --mb 512 --duration 30
```

Network server on the VM:

```bash
python3 test_workloads/workload.py net-server --host 0.0.0.0 --port 9000
```

Network client from another machine or VM:

```bash
python3 test_workloads/workload.py net-client --host <vm-ip-address> --port 9000 --mb 256
```

The agent currently excludes the loopback interface `lo`, so traffic to
`127.0.0.1` will not increase the network byte-rate metrics.

Disk write activity:

```bash
dd if=/dev/zero of=/tmp/telemetry-disk-test bs=1M count=512 conv=fdatasync
```

Expected effect:

```text
disk_write_bytes_per_second increases
disk_writes_per_second may increase
```

## Why These Metrics Matter

CPU, memory, and network metrics are the first signals for understanding node
health.

```text
High CPU usage        -> app or kernel work may be saturating the node
Low MemAvailable      -> memory pressure may cause latency or OOM kills
High network traffic  -> workload or dependency traffic may be increasing
TCP retransmits       -> packet loss, congestion, or unreliable network path
High disk writes      -> logging, database, or storage pressure
Top CPU/memory PIDs   -> which process is likely responsible
```

Node-level metrics tell us what is happening on the Linux VM as a whole.
Container cgroups connect host PIDs and resource usage to Kubernetes pod and
workload identity. Kubernetes desired state and Events add the orchestration
context Linux cannot provide. The demo's OpenTelemetry instrumentation now
captures request dependencies and attaches Kubernetes pod, container,
namespace, and node identity to each trace and request metric. Service-to-pod
topology derived from the Kubernetes API is still a separate future mapping.

## Roadmap

Project milestones:

1. Validate CPU, memory, network, TCP, disk, and process behavior.
2. Install k3s on the VM and deploy the included `frontend -> checkout -> payment`
   application.
3. Calculate per-container rates from the raw cgroup counters.
4. Aggregate pod metrics into workload/Deployment summaries. **Complete.**
5. Collect Kubernetes lifecycle events. **Complete.**
6. Add fault injection for CPU saturation, memory pressure, network loss, and
   service crashes.
7. Collect OpenTelemetry traces and application request metrics. **Complete.**
8. Persist application telemetry, map Services to selected pods, and correlate
   request symptoms with the existing Linux/container time series.

The larger goal is to correlate low-level telemetry with service dependencies
so the system can eventually distinguish root causes from downstream symptoms.

## Kubernetes Demo Application

The first Kubernetes workload is a three-service FastAPI application:

```text
frontend -> checkout -> payment
```

See [`demo_app/README.md`](demo_app/README.md) for the Ubuntu VM, k3s, image
build, deployment, and verification commands.
