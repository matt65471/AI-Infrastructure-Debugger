#include "cpu_collector.h"
#include "mem_collector.h"

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <thread>

int main() {
    try {
        CpuCollector cpu_collector;
        MemCollector mem_collector;
        CpuSample previous = cpu_collector.read_sample();

        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            const CpuSample current = cpu_collector.read_sample();
            const double cpu_usage =
                cpu_collector.calculate_utilization(previous, current);

            std::cout << "cpu_usage_percent=" << std::fixed
                      << std::setprecision(2) << cpu_usage << '\n';
            
            const MemSample mem = mem_collector.read_sample();

            previous = current;
        }
    } catch (const std::exception& error) {
        std::cerr << "telemetry_agent error: " << error.what() << '\n';
        return 1;
    }
}
