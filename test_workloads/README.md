# Test Workloads

These small workloads are for testing the telemetry agent on a Linux VM.
They intentionally create CPU, memory, or network activity so you can watch
the agent metrics change.

Run the telemetry agent in one terminal:

```bash
cd ~/AI-Infrastructure-Debugger
./agent/build/telemetry_agent
```

Then run one of these workloads in another terminal.

## CPU Load

Burn CPU for 30 seconds:

```bash
python3 test_workloads/workload.py cpu --workers 1 --duration 30
```

Expected agent effect:

```text
cpu_usage_percent increases
```

Use more workers to stress more CPU cores:

```bash
python3 test_workloads/workload.py cpu --workers 4 --duration 30
```

## Memory Load

Allocate and hold 512 MiB for 30 seconds:

```bash
python3 test_workloads/workload.py memory --mb 512 --duration 30
```

Expected agent effect:

```text
memory_usage_percent increases
memory_available_kb decreases
```

Start small on tiny VMs. Allocating too much memory can make the VM slow or
trigger the Linux OOM killer.

## Network Load

For a quick smoke test, start a local server:

```bash
python3 test_workloads/workload.py net-server --host 127.0.0.1 --port 9000
```

In another terminal, send traffic to it:

```bash
python3 test_workloads/workload.py net-client --host 127.0.0.1 --port 9000 --mb 256
```

Expected agent effect:

```text
network_rx_bytes_per_second increases
network_tx_bytes_per_second increases
```

Note: the telemetry agent currently excludes the loopback interface `lo` from
network totals. If you use `127.0.0.1`, the client/server works, but you should
not expect the agent's network counters to change.

To test the agent's network metrics, use traffic that crosses the VM's network
interface. On the VM, find its IP address:

```bash
hostname -I
```

Then start the server on the VM:

```bash
python3 test_workloads/workload.py net-server --host 0.0.0.0 --port 9000
```

From your host machine or another VM, connect to the VM IP:

```bash
python3 test_workloads/workload.py net-client --host <vm-ip-address> --port 9000 --mb 256
```

That should increase:

```text
network_rx_bytes_per_second
network_tx_bytes_per_second
```
