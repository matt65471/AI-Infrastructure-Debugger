#include "telemetry_collector.h"

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string format_process_list(const std::vector<ProcessMetric>& processes,
                                bool include_cpu) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < processes.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }

        const ProcessMetric& process = processes[index];
        stream << process.pid << ':' << process.name << ':';
        if (include_cpu) {
            stream << std::fixed << std::setprecision(2)
                   << process.cpu_usage_percent;
        } else {
            stream << process.resident_memory_kb;
        }
    }
    stream << ']';
    return stream.str();
}

std::string format_pid_list(const std::vector<int>& process_ids) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < process_ids.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << process_ids[index];
    }
    stream << ']';
    return stream.str();
}

std::string short_container_id(const std::string& container_id) {
    constexpr std::size_t kShortIdLength = 12;
    return container_id.size() <= kShortIdLength
               ? container_id
               : container_id.substr(0, kShortIdLength);
}

std::string format_container_list(
    const std::vector<ContainerMetric>& containers) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < containers.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }

        const ContainerMetric& container = containers[index];
        stream << "{container_id="
               << short_container_id(container.container_id)
               << ",path=" << container.cgroup_path
               << ",cpu_usage_percent=";
        if (container.cpu_usage_available) {
            stream << std::fixed << std::setprecision(2)
                   << container.cpu_usage_percent;
        } else {
            stream << "na";
        }
        stream << ",cpu_usage_usec=" << container.cpu_usage_usec
               << ",throttled_periods_delta="
               << container.throttled_periods_delta
               << ",throttled_usec_delta="
               << container.throttled_usec_delta
               << ",memory_current_bytes="
               << container.memory_current_bytes
               << ",memory_max=";
        if (container.memory_is_unlimited) {
            stream << "max";
        } else {
            stream << container.memory_max_bytes;
        }
        stream << ",memory_usage_percent=";
        if (container.memory_usage_percent_available) {
            stream << std::fixed << std::setprecision(2)
                   << container.memory_usage_percent;
        } else {
            stream << "na";
        }
        stream << ",oom_delta=" << container.oom_delta
               << ",oom_kill_delta=" << container.oom_kill_delta
               << ",pids=" << format_pid_list(container.process_ids) << '}';
    }
    stream << ']';
    return stream.str();
}

}  // namespace

int main() {
    try {
        TelemetryCollector telemetry_collector;

        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            const TelemetrySnapshot snapshot = telemetry_collector.collect();
            const NodeMetric& node = snapshot.node;
            std::cout << "timestamp_unix_ms=" << node.timestamp_unix_ms
                      << " node=" << node.hostname
                      << " cpu_usage_percent=" << std::fixed
                      << std::setprecision(2) << node.cpu_usage_percent
                      << " memory_usage_percent="
                      << node.memory_usage_percent
                      << " memory_available_kb="
                      << node.memory_available_kb
                      << " network_rx_bytes_per_second="
                      << node.network_rx_bytes_per_second
                      << " network_tx_bytes_per_second="
                      << node.network_tx_bytes_per_second
                      << " tcp_retransmits_per_second="
                      << node.tcp_retransmits_per_second
                      << " tcp_in_segments_per_second="
                      << node.tcp_in_segments_per_second
                      << " tcp_out_segments_per_second="
                      << node.tcp_out_segments_per_second
                      << " tcp_reset_count_delta="
                      << node.tcp_reset_count_delta
                      << " tcp_listen_overflows_delta="
                      << node.tcp_listen_overflows_delta
                      << " tcp_listen_drops_delta="
                      << node.tcp_listen_drops_delta
                      << " tcp_timeouts_delta="
                      << node.tcp_timeouts_delta
                      << " disk_read_bytes_per_second="
                      << node.disk_read_bytes_per_second
                      << " disk_write_bytes_per_second="
                      << node.disk_write_bytes_per_second
                      << " disk_reads_per_second="
                      << node.disk_reads_per_second
                      << " disk_writes_per_second="
                      << node.disk_writes_per_second
                      << " disk_io_time_ms_delta="
                      << node.disk_io_time_ms_delta
                      << " top_cpu="
                      << format_process_list(snapshot.top_cpu_processes, true)
                      << " top_memory="
                      << format_process_list(snapshot.top_memory_processes, false)
                      << " cgroup_v2="
                      << (snapshot.cgroups.cgroup_v2_available ? "true" : "false")
                      << " containers="
                      << format_container_list(snapshot.containers)
                      << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "telemetry_agent error: " << error.what() << '\n';
        return 1;
    }
}
