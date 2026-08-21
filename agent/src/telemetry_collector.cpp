#include "telemetry_collector.h"

#include <algorithm>
#include <unordered_map>

namespace {

constexpr std::uint64_t kSectorSizeBytes = 512;
constexpr std::size_t kTopProcessCount = 5;

std::uint64_t total_cpu_time(const CpuSample& sample) {
    return sample.user + sample.nice + sample.system + sample.idle +
           sample.iowait + sample.irq + sample.softirq + sample.steal;
}

std::uint64_t positive_delta(std::uint64_t previous, std::uint64_t current) {
    if (current < previous) {
        return 0;
    }
    return current - previous;
}

std::uint64_t aggregate_reads(const DiskSample& sample) {
    std::uint64_t total = 0;
    for (const DiskDeviceSample& device : sample.devices) {
        total += device.reads_completed;
    }
    return total;
}

std::uint64_t aggregate_writes(const DiskSample& sample) {
    std::uint64_t total = 0;
    for (const DiskDeviceSample& device : sample.devices) {
        total += device.writes_completed;
    }
    return total;
}

std::uint64_t aggregate_read_bytes(const DiskSample& sample) {
    std::uint64_t total = 0;
    for (const DiskDeviceSample& device : sample.devices) {
        total += device.sectors_read * kSectorSizeBytes;
    }
    return total;
}

std::uint64_t aggregate_write_bytes(const DiskSample& sample) {
    std::uint64_t total = 0;
    for (const DiskDeviceSample& device : sample.devices) {
        total += device.sectors_written * kSectorSizeBytes;
    }
    return total;
}

std::uint64_t aggregate_io_time_ms(const DiskSample& sample) {
    std::uint64_t total = 0;
    for (const DiskDeviceSample& device : sample.devices) {
        total += device.io_time_ms;
    }
    return total;
}

std::unordered_map<int, ProcessSample> process_map(
    const ProcessCollectionSample& sample) {
    std::unordered_map<int, ProcessSample> processes;
    for (const ProcessSample& process : sample.processes) {
        processes[process.pid] = process;
    }
    return processes;
}

std::vector<ProcessMetric> build_process_metrics(
    const ProcessCollectionSample& previous,
    const ProcessCollectionSample& current,
    std::uint64_t system_cpu_delta) {
    const std::unordered_map<int, ProcessSample> previous_processes =
        process_map(previous);
    std::vector<ProcessMetric> metrics;

    for (const ProcessSample& current_process : current.processes) {
        ProcessMetric metric;
        metric.pid = current_process.pid;
        metric.name = current_process.name;
        metric.state = current_process.state;
        metric.resident_memory_kb = current_process.resident_memory_kb;
        metric.virtual_memory_kb = current_process.virtual_memory_kb;
        metric.thread_count = current_process.thread_count;

        const auto previous_iterator = previous_processes.find(current_process.pid);
        if (previous_iterator != previous_processes.end()) {
            const ProcessSample& previous_process = previous_iterator->second;
            const std::uint64_t process_cpu_delta = positive_delta(
                previous_process.cpu_ticks,
                current_process.cpu_ticks);
            if (system_cpu_delta > 0) {
                metric.cpu_usage_percent =
                    100.0 * static_cast<double>(process_cpu_delta) /
                    static_cast<double>(system_cpu_delta);
            }
            metric.read_bytes_per_second = positive_delta(
                previous_process.read_bytes,
                current_process.read_bytes);
            metric.write_bytes_per_second = positive_delta(
                previous_process.write_bytes,
                current_process.write_bytes);
        }

        metrics.push_back(metric);
    }

    return metrics;
}

std::vector<ProcessMetric> top_by_cpu(std::vector<ProcessMetric> metrics) {
    std::sort(metrics.begin(), metrics.end(),
              [](const ProcessMetric& left, const ProcessMetric& right) {
                  return left.cpu_usage_percent > right.cpu_usage_percent;
              });
    if (metrics.size() > kTopProcessCount) {
        metrics.resize(kTopProcessCount);
    }
    return metrics;
}

std::vector<ProcessMetric> top_by_memory(std::vector<ProcessMetric> metrics) {
    std::sort(metrics.begin(), metrics.end(),
              [](const ProcessMetric& left, const ProcessMetric& right) {
                  return left.resident_memory_kb > right.resident_memory_kb;
              });
    if (metrics.size() > kTopProcessCount) {
        metrics.resize(kTopProcessCount);
    }
    return metrics;
}

}  // namespace

TelemetryCollector::TelemetryCollector()
    : previous_cpu_sample_(cpu_collector_.read_sample()),
      previous_network_sample_(network_collector_.read_sample()),
      previous_tcp_sample_(tcp_collector_.read_sample()),
      previous_disk_sample_(disk_collector_.read_sample()),
      previous_process_sample_(process_collector_.read_sample()) {}

TelemetrySnapshot TelemetryCollector::collect() {
    const CpuSample current_cpu_sample = cpu_collector_.read_sample();
    const MemorySample memory_sample = memory_collector_.read_sample();
    const NetworkSample current_network_sample = network_collector_.read_sample();
    const TcpSample current_tcp_sample = tcp_collector_.read_sample();
    const DiskSample current_disk_sample = disk_collector_.read_sample();
    const ProcessCollectionSample current_process_sample =
        process_collector_.read_sample();
    const std::uint64_t system_cpu_delta = positive_delta(
        total_cpu_time(previous_cpu_sample_),
        total_cpu_time(current_cpu_sample));

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
    snapshot.tcp_retransmits_per_second = positive_delta(
        previous_tcp_sample_.retransmitted_segments,
        current_tcp_sample.retransmitted_segments);
    snapshot.tcp_in_segments_per_second = positive_delta(
        previous_tcp_sample_.in_segments,
        current_tcp_sample.in_segments);
    snapshot.tcp_out_segments_per_second = positive_delta(
        previous_tcp_sample_.out_segments,
        current_tcp_sample.out_segments);
    snapshot.tcp_reset_count_delta = positive_delta(
        previous_tcp_sample_.resets_sent,
        current_tcp_sample.resets_sent);
    snapshot.tcp_listen_overflows_delta = positive_delta(
        previous_tcp_sample_.listen_overflows,
        current_tcp_sample.listen_overflows);
    snapshot.tcp_listen_drops_delta = positive_delta(
        previous_tcp_sample_.listen_drops,
        current_tcp_sample.listen_drops);
    snapshot.tcp_timeouts_delta = positive_delta(
        previous_tcp_sample_.timeouts,
        current_tcp_sample.timeouts);
    snapshot.disk_read_bytes_per_second = positive_delta(
        aggregate_read_bytes(previous_disk_sample_),
        aggregate_read_bytes(current_disk_sample));
    snapshot.disk_write_bytes_per_second = positive_delta(
        aggregate_write_bytes(previous_disk_sample_),
        aggregate_write_bytes(current_disk_sample));
    snapshot.disk_reads_per_second = positive_delta(
        aggregate_reads(previous_disk_sample_),
        aggregate_reads(current_disk_sample));
    snapshot.disk_writes_per_second = positive_delta(
        aggregate_writes(previous_disk_sample_),
        aggregate_writes(current_disk_sample));
    snapshot.disk_io_time_ms_delta = positive_delta(
        aggregate_io_time_ms(previous_disk_sample_),
        aggregate_io_time_ms(current_disk_sample));

    const std::vector<ProcessMetric> process_metrics = build_process_metrics(
        previous_process_sample_,
        current_process_sample,
        system_cpu_delta);
    snapshot.top_cpu_processes = top_by_cpu(process_metrics);
    snapshot.top_memory_processes = top_by_memory(process_metrics);

    previous_cpu_sample_ = current_cpu_sample;
    previous_network_sample_ = current_network_sample;
    previous_tcp_sample_ = current_tcp_sample;
    previous_disk_sample_ = current_disk_sample;
    previous_process_sample_ = current_process_sample;

    return snapshot;
}
