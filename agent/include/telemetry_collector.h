#ifndef TELEMETRY_COLLECTOR_H
#define TELEMETRY_COLLECTOR_H

#include "cpu_collector.h"
#include "mem_collector.h"
#include "network_collector.h"

#include <cstdint>

struct TelemetrySnapshot {
    double cpu_usage_percent = 0.0;
    double memory_usage_percent = 0.0;
    std::uint64_t memory_available_kb = 0;
    std::uint64_t network_rx_bytes_per_second = 0;
    std::uint64_t network_tx_bytes_per_second = 0;
};

class TelemetryCollector {
public:
    TelemetryCollector();
    TelemetrySnapshot collect();

private:
    CpuCollector cpu_collector_;
    MemoryCollector memory_collector_;
    NetworkCollector network_collector_;
    CpuSample previous_cpu_sample_;
    NetworkSample previous_network_sample_;
};

#endif
