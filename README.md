# AI Infrastructure Debugger

This project is an incremental systems project for learning how Linux,
infrastructure, observability, and eventually Kubernetes/root-cause analysis fit
together.

The long-term goal is to build a debugger that can monitor distributed
applications, detect failures, and identify likely root causes from telemetry
and service dependencies.

The project currently contains a C++ Linux telemetry agent and a small
Kubernetes application used to generate service-to-service behavior. A local
web dashboard presents the combined telemetry as a drill-down hierarchy.

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
- storage
- databases
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
└── demo-app.yaml

dashboard/
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

For the dashboard, `--json-file PATH` atomically replaces one machine-readable
snapshot file every second. The dashboard server reads that file and never
needs root access or Kubernetes credentials. `--json` writes the same document
to standard output for other integrations.

Example output:

```text
timestamp_unix_ms=1788217200000 node=ubuntu-vm cpu_usage_percent=3.20 memory_usage_percent=41.75 memory_available_kb=4045320 network_rx_bytes_per_second=1204 network_tx_bytes_per_second=884 tcp_retransmits_per_second=0 disk_read_bytes_per_second=0 disk_write_bytes_per_second=4096 load_average_1m=0.20 cpu_pressure={some={avg10=0.00,...},full=na} memory_pressure={some={...},full={...}} io_pressure={some={...},full={...}} top_cpu=[1234:payment:12.40] top_memory=[1234:payment:524288] cgroup_v2=true kubernetes_metadata=available containers=[...] pods=[...] deployments=[...] kubernetes_events=[...]
```

Each `TelemetrySnapshot` contains one explicit `NodeMetric` plus separate
process and cgroup collections. The node record adds the collection time in
Unix milliseconds and the Linux hostname to the existing host-wide CPU,
memory, network, TCP, and disk metrics. The hostname is local Linux identity;
each matched container also reports the node name assigned by Kubernetes.

## Build And Run

This project must run on Linux because it reads Linux-specific virtual files
under `/proc`. Currently, I am running this on a Linux VM on my Mac using UTM and the latest Linux version downloaded
from Ubuntu.

Install build tools on an Ubuntu VM:

```bash
sudo apt update
sudo apt install -y build-essential cmake git python3 python3-venv
```

Build and run:

```bash
cd ~/AI-Infrastructure-Debugger
rm -rf agent/build
cmake -S agent -B agent/build
cmake --build agent/build
ctest --test-dir agent/build --output-on-failure
sudo ./agent/build/telemetry_agent
```

Stop the agent with `Ctrl+C`.

To use the drill-down dashboard, run the agent and dashboard in separate VM
terminals:

```bash
sudo ./agent/build/telemetry_agent \
  --json-file /tmp/ai-infrastructure-debugger-snapshot.json
```

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r demo_app/requirements.txt
python3 dashboard/server.py --host 0.0.0.0 --port 8080
```

Then open `http://<vm-ip-address>:8080` from the Mac. See
[`dashboard/README.md`](dashboard/README.md) for the live and sample-data modes.

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
it can read all host processes and use k3s cluster credentials:

```bash
sudo ./agent/build/telemetry_agent
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
context Linux cannot provide. Kubernetes Service selection and request
dependencies are not mapped yet.

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
7. Map Services to selected pods and add request/dependency attribution.

The larger goal is to correlate low-level telemetry with service dependencies
so the system can eventually distinguish root causes from downstream symptoms.

## Kubernetes Demo Application

The first Kubernetes workload is a three-service FastAPI application:

```text
frontend -> checkout -> payment
```

See [`demo_app/README.md`](demo_app/README.md) for the Ubuntu VM, k3s, image
build, deployment, and verification commands.
