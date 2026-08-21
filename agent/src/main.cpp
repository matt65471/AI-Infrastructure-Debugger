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

}  // namespace

int main() {
    try {
        TelemetryCollector telemetry_collector;

        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            const TelemetrySnapshot snapshot = telemetry_collector.collect();
            std::cout << "cpu_usage_percent=" << std::fixed
                      << std::setprecision(2) << snapshot.cpu_usage_percent
                      << " memory_usage_percent="
                      << snapshot.memory_usage_percent
                      << " memory_available_kb="
                      << snapshot.memory_available_kb
                      << " network_rx_bytes_per_second="
                      << snapshot.network_rx_bytes_per_second
                      << " network_tx_bytes_per_second="
                      << snapshot.network_tx_bytes_per_second
                      << " tcp_retransmits_per_second="
                      << snapshot.tcp_retransmits_per_second
                      << " tcp_in_segments_per_second="
                      << snapshot.tcp_in_segments_per_second
                      << " tcp_out_segments_per_second="
                      << snapshot.tcp_out_segments_per_second
                      << " tcp_reset_count_delta="
                      << snapshot.tcp_reset_count_delta
                      << " tcp_listen_overflows_delta="
                      << snapshot.tcp_listen_overflows_delta
                      << " tcp_listen_drops_delta="
                      << snapshot.tcp_listen_drops_delta
                      << " tcp_timeouts_delta="
                      << snapshot.tcp_timeouts_delta
                      << " disk_read_bytes_per_second="
                      << snapshot.disk_read_bytes_per_second
                      << " disk_write_bytes_per_second="
                      << snapshot.disk_write_bytes_per_second
                      << " disk_reads_per_second="
                      << snapshot.disk_reads_per_second
                      << " disk_writes_per_second="
                      << snapshot.disk_writes_per_second
                      << " disk_io_time_ms_delta="
                      << snapshot.disk_io_time_ms_delta
                      << " top_cpu="
                      << format_process_list(snapshot.top_cpu_processes, true)
                      << " top_memory="
                      << format_process_list(snapshot.top_memory_processes, false)
                      << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "telemetry_agent error: " << error.what() << '\n';
        return 1;
    }
}
