# AI Infrastructure Debugger

This project is an incremental systems project for learning how Linux,
infrastructure, observability, and eventually Kubernetes/root-cause analysis fit
together.

The long-term goal is to build a debugger that can monitor distributed
applications, detect failures, and identify likely root causes from telemetry
and service dependencies.

For now, the project is intentionally small: a C++ Linux telemetry agent that
reads raw Linux interfaces directly.

## Current Phase

Phase 1 is a normal Linux process that collects node-level telemetry once per
second.

Current metrics:

- CPU utilization from `/proc/stat`
- memory utilization from `/proc/meminfo`
- network receive/transmit byte rates from `/proc/net/dev`
- TCP/IP counters from `/proc/net/snmp` and `/proc/net/netstat`
- disk I/O counters from `/proc/diskstats`
- top process CPU and memory metrics from `/proc/[pid]`

Not included yet:

- Kubernetes
- containers/cgroups
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
│   ├── disk_collector.h
│   ├── mem_collector.h
│   ├── network_collector.h
│   ├── process_collector.h
│   ├── tcp_collector.h
│   └── telemetry_collector.h
└── src/
    ├── cpu_collector.cpp
    ├── disk_collector.cpp
    ├── main.cpp
    ├── mem_collector.cpp
    ├── network_collector.cpp
    ├── process_collector.cpp
    ├── tcp_collector.cpp
    └── telemetry_collector.cpp

test_workloads/
├── README.md
└── workload.py
```

## How The Agent Works

The collectors each own one Linux data source:

```text
CpuCollector       -> /proc/stat
MemoryCollector    -> /proc/meminfo
NetworkCollector   -> /proc/net/dev
TcpCollector       -> /proc/net/snmp, /proc/net/netstat
DiskCollector      -> /proc/diskstats
ProcessCollector   -> /proc/[pid]/stat, /proc/[pid]/status, /proc/[pid]/io
TelemetryCollector -> coordinates the collectors
```

`main.cpp` does not parse Linux files directly. It creates a
`TelemetryCollector`, calls `collect()` once per second, and prints the combined
snapshot.

Example output:

```text
cpu_usage_percent=3.20 memory_usage_percent=41.75 memory_available_kb=4045320 network_rx_bytes_per_second=1204 network_tx_bytes_per_second=884 tcp_retransmits_per_second=0 disk_read_bytes_per_second=0 disk_write_bytes_per_second=4096 top_cpu=[1234:payment:12.40] top_memory=[1234:payment:524288]
```

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
./agent/build/telemetry_agent
```

Stop the agent with `Ctrl+C`.

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

These are node-level metrics. They tell us what is happening on the Linux VM as
a whole. Process metrics add the first layer of attribution, but the agent does
not yet map processes to containers, pods, or services.

## Roadmap

My next goals:

1. Validate CPU, memory, network, TCP, disk, and process behavior.
2. Add cgroup/container metrics from `/sys/fs/cgroup`.
3. Install a lightweight Kubernetes distribution such as k3s on the VM.
4. Deploy a small test application, for example `frontend -> checkout -> payment`.
5. Map Linux/cgroup telemetry back to Kubernetes pods and services.
6. Add fault injection for CPU saturation, memory pressure, network loss, and
   service crashes.

The larger goal is to correlate low-level telemetry with service dependencies
so the system can eventually distinguish root causes from downstream symptoms.
