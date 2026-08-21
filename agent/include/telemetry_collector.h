#ifndef TELEMETRY_COLLECTOR_H
#define TELEMETRY_COLLECTOR_H

#include "cpu_collector.h"
#include "disk_collector.h"
#include "mem_collector.h"
#include "network_collector.h"
#include "process_collector.h"
#include "tcp_collector.h"

#include <cstdint>
#include <string>
#include <vector>

struct ProcessMetric {
    int pid = 0;
    std::string name;
    char state = '?';
    double cpu_usage_percent = 0.0;
    std::uint64_t resident_memory_kb = 0;
    std::uint64_t virtual_memory_kb = 0;
    std::uint64_t thread_count = 0;
    std::uint64_t read_bytes_per_second = 0;
    std::uint64_t write_bytes_per_second = 0;
};

struct TelemetrySnapshot {
    double cpu_usage_percent = 0.0;
    double memory_usage_percent = 0.0;
    std::uint64_t memory_available_kb = 0;
    std::uint64_t network_rx_bytes_per_second = 0;
    std::uint64_t network_tx_bytes_per_second = 0;
    std::uint64_t tcp_retransmits_per_second = 0;
    std::uint64_t tcp_in_segments_per_second = 0;
    std::uint64_t tcp_out_segments_per_second = 0;
    std::uint64_t tcp_reset_count_delta = 0;
    std::uint64_t tcp_listen_overflows_delta = 0;
    std::uint64_t tcp_listen_drops_delta = 0;
    std::uint64_t tcp_timeouts_delta = 0;
    std::uint64_t disk_read_bytes_per_second = 0;
    std::uint64_t disk_write_bytes_per_second = 0;
    std::uint64_t disk_reads_per_second = 0;
    std::uint64_t disk_writes_per_second = 0;
    std::uint64_t disk_io_time_ms_delta = 0;
    std::vector<ProcessMetric> top_cpu_processes;
    std::vector<ProcessMetric> top_memory_processes;
};

class TelemetryCollector {
public:
    TelemetryCollector();
    TelemetrySnapshot collect();

private:
    CpuCollector cpu_collector_;
    MemoryCollector memory_collector_;
    NetworkCollector network_collector_;
    TcpCollector tcp_collector_;
    DiskCollector disk_collector_;
    ProcessCollector process_collector_;
    CpuSample previous_cpu_sample_;
    NetworkSample previous_network_sample_;
    TcpSample previous_tcp_sample_;
    DiskSample previous_disk_sample_;
    ProcessCollectionSample previous_process_sample_;
};

#endif
