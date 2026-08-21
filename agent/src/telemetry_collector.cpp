#include "telemetry_collector.h"

TelemetryCollector::TelemetryCollector()
    : previous_cpu_sample_(cpu_collector_.read_sample()),
      previous_network_sample_(network_collector_.read_sample()) {}

TelemetrySnapshot TelemetryCollector::collect() {
    const CpuSample current_cpu_sample = cpu_collector_.read_sample();
    const MemorySample memory_sample = memory_collector_.read_sample();
    const NetworkSample current_network_sample = network_collector_.read_sample();

    TelemetrySnapshot snapshot;
    snapshot.cpu_usage_percent = cpu_collector_.calculate_utilization(
        previous_cpu_sample_,
        current_cpu_sample);
    snapshot.memory_usage_percent =
        memory_collector_.calculate_utilization(memory_sample);
    snapshot.memory_available_kb = memory_sample.mem_available_kb;
    snapshot.network_rx_bytes_per_second =
        network_collector_.calculate_rx_bytes_per_second(
            previous_network_sample_,
            current_network_sample);
    snapshot.network_tx_bytes_per_second =
        network_collector_.calculate_tx_bytes_per_second(
            previous_network_sample_,
            current_network_sample);

    previous_cpu_sample_ = current_cpu_sample;
    previous_network_sample_ = current_network_sample;

    return snapshot;
}
