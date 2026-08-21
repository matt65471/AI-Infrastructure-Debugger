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

Not included yet:

- Kubernetes
- containers/cgroups
- per-process attribution
- TCP retransmission stats
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
│   ├── mem_collector.h
│   ├── network_collector.h
│   └── telemetry_collector.h
└── src/
    ├── cpu_collector.cpp
    ├── main.cpp
    ├── mem_collector.cpp
    ├── network_collector.cpp
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
TelemetryCollector -> coordinates the collectors
```

`main.cpp` does not parse Linux files directly. It creates a
`TelemetryCollector`, calls `collect()` once per second, and prints the combined
snapshot.

Example output:

```text
cpu_usage_percent=3.20 memory_usage_percent=41.75 memory_available_kb=4045320 network_rx_bytes_per_second=1204 network_tx_bytes_per_second=884
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

## Why These Metrics Matter

CPU, memory, and network metrics are the first signals for understanding node
health.

```text
High CPU usage        -> app or kernel work may be saturating the node
Low MemAvailable      -> memory pressure may cause latency or OOM kills
High network traffic  -> workload or dependency traffic may be increasing
Network drops/errors  -> later signal for packet loss or interface trouble
```

These are node-level metrics. They tell us what is happening on the Linux VM as
a whole, not yet which process, container, pod, or service caused it.

## Roadmap

My next goals:

1. Validate CPU, memory, and network behavior with the test workloads.
2. Add per-process metrics from `/proc/[pid]`.
3. Add cgroup/container metrics from `/sys/fs/cgroup`.
4. Install a lightweight Kubernetes distribution such as k3s on the VM.
5. Deploy a small test application, for example `frontend -> checkout -> payment`.
6. Map Linux/cgroup telemetry back to Kubernetes pods and services.
7. Add fault injection for CPU saturation, memory pressure, network loss, and
   service crashes.

The larger goal is to correlate low-level telemetry with service dependencies
so the system can eventually distinguish root causes from downstream symptoms.
