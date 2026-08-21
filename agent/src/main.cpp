#include "telemetry_collector.h"

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <thread>

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
                      << snapshot.network_tx_bytes_per_second << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "telemetry_agent error: " << error.what() << '\n';
        return 1;
    }
}
