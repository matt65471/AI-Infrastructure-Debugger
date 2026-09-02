# AI Infrastructure Debugger

This project is an incremental systems project for learning how Linux,
infrastructure, observability, and eventually Kubernetes/root-cause analysis fit
together.

The long-term goal is to build a debugger that can monitor distributed
applications, detect failures, and identify likely root causes from telemetry
and service dependencies.

The project currently contains a C++ Linux telemetry agent and a small
Kubernetes application used to generate service-to-service behavior.

## Current Phase

The Linux agent collects node-level telemetry once per second. The next VM
milestone deploys the included Kubernetes demo application on a single-node
k3s cluster.

Current metrics:

- CPU utilization from `/proc/stat`
- memory utilization from `/proc/meminfo`
- network receive/transmit byte rates from `/proc/net/dev`
- TCP/IP counters from `/proc/net/snmp` and `/proc/net/netstat`
- disk I/O counters from `/proc/diskstats`
- top process CPU and memory metrics from `/proc/[pid]`
- raw Kubernetes container cgroup v2 counters from `/sys/fs/cgroup`
- calculated per-container CPU, throttling, memory, and OOM metrics
- Kubernetes node, workload, pod, and container identity joined by container ID
- pod-level CPU, memory, throttling, OOM, restart, container, and process aggregates

Not included yet:

- Kubernetes lifecycle event history and Service dependency attribution
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
│   ├── disk_collector.h
│   ├── kubernetes_metadata_collector.h
│   ├── mem_collector.h
│   ├── network_collector.h
│   ├── pod_metric_aggregator.h
│   ├── process_collector.h
│   ├── tcp_collector.h
│   └── telemetry_collector.h
└── src/
    ├── container_metric_calculator.cpp
    ├── kubernetes_metadata_collector.cpp
    ├── pod_metric_aggregator.cpp
    ├── linux/
    │   ├── cgroup_collector.cpp
    │   ├── cpu_collector.cpp
    │   ├── disk_collector.cpp
    │   ├── mem_collector.cpp
    │   ├── network_collector.cpp
    │   ├── process_collector.cpp
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
TelemetryCollector -> coordinates the collectors
```

The cgroup collector detects cgroup v2, discovers host processes in `kubepods`
cgroups, and reads raw `cpu.stat`, `memory.current`, `memory.max`,
`memory.events`, and `cgroup.procs` values. The container metric calculator
matches consecutive samples by full container ID and calculates CPU usage,
throttling and OOM deltas, and memory utilization. The Kubernetes metadata
collector refreshes every five seconds and uses the full container ID to attach
node, namespace, pod UID/name, container name/image, readiness, restart count,
and owning workload. For this VM stage it invokes the local k3s `kubectl`; a
future agent service will use the Kubernetes API directly with a service
account.

The pod metric aggregator groups Kubernetes-matched containers by pod UID. It
sums CPU, memory, throttling, OOM, and restart values, combines container IDs
and host PIDs, and requires every included container to be ready before the pod
aggregate is marked ready. The existing node metrics continue to come directly
from Linux host counters; they are not calculated by summing pods, which would
omit Kubernetes and host overhead.

`main.cpp` does not parse Linux files directly. It creates a
`TelemetryCollector`, calls `collect()` once per second, and prints the combined
snapshot.

Example output:

```text
timestamp_unix_ms=1788217200000 node=ubuntu-vm cpu_usage_percent=3.20 memory_usage_percent=41.75 memory_available_kb=4045320 network_rx_bytes_per_second=1204 network_tx_bytes_per_second=884 tcp_retransmits_per_second=0 disk_read_bytes_per_second=0 disk_write_bytes_per_second=4096 top_cpu=[1234:payment:12.40] top_memory=[1234:payment:524288] cgroup_v2=true kubernetes_metadata=available containers=[...] pods=[...]
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
sudo apt install -y build-essential cmake git python3
```

Build and run:

```bash
cd ~/AI-Infrastructure-Debugger
rm -rf agent/build
cmake -S agent -B agent/build
cmake --build agent/build
sudo ./agent/build/telemetry_agent
```

Stop the agent with `Ctrl+C`.

With k3s and the demo application running, container calculation, Kubernetes
identity, and pod aggregation are working when the output contains:

```text
cgroup_v2=true kubernetes_metadata=available containers=[...] pods=[...]
```

Each entry reports a shortened container ID, host cgroup path, CPU percentage,
cumulative CPU time, throttling deltas, current and maximum memory, memory
percentage, OOM deltas, and host PIDs. `100%` CPU means one fully used core and
a multi-core container can exceed `100%`. A newly seen container reports `na`
for CPU until it has two samples; unlimited memory reports `na` for memory
percentage. Run the agent with `sudo` on the k3s VM so it can read all host
processes and use k3s cluster credentials:

```bash
sudo ./agent/build/telemetry_agent
```

When Kubernetes identity matches, each container entry also includes:

```text
kubernetes={node=ubuntu-vm,namespace=infrastructure-demo,pod=payment-...,pod_uid=...,container=payment,image=infrastructure-debugger/payment:v1,phase=Running,ready=true,restarts=0,workload=Deployment/payment}
```

Pod sandbox containers and containers that have not appeared in the latest
Kubernetes metadata refresh remain visible with `kubernetes=unmatched`.
Unmatched containers are not included in pod aggregates because they have no
reliable pod UID.

A pod aggregate resembles:

```text
{node=ubuntu-vm,namespace=infrastructure-demo,pod=payment-...,pod_uid=...,workload=Deployment/payment,phase=Running,ready=true,container_count=1,container_names=[payment],container_ids=[91ab2345cdef],cpu_usage_percent=12.40,memory_current_bytes=73400320,memory_max=134217728,memory_usage_percent=54.69,oom_kill_delta=0,restarts=0,pids=[1488]}
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
workload identity. Kubernetes Service selection and request dependencies are
not mapped yet.

## Roadmap

Project milestones:

1. Validate CPU, memory, network, TCP, disk, and process behavior.
2. Install k3s on the VM and deploy the included `frontend -> checkout -> payment`
   application.
3. Calculate per-container rates from the raw cgroup counters.
4. Aggregate pod metrics into workload/Deployment summaries.
5. Collect Kubernetes lifecycle events and map Services to selected pods.
6. Add fault injection for CPU saturation, memory pressure, network loss, and
   service crashes.

The larger goal is to correlate low-level telemetry with service dependencies
so the system can eventually distinguish root causes from downstream symptoms.

## Kubernetes Demo Application

The first Kubernetes workload is a three-service FastAPI application:

```text
frontend -> checkout -> payment
```

See [`demo_app/README.md`](demo_app/README.md) for the Ubuntu VM, k3s, image
build, deployment, and verification commands.
