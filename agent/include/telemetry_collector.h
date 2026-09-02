#ifndef TELEMETRY_COLLECTOR_H
#define TELEMETRY_COLLECTOR_H

#include "cgroup_collector.h"
#include "container_metric_calculator.h"
#include "cpu_collector.h"
#include "deployment_metric_aggregator.h"
#include "disk_collector.h"
#include "kubernetes_metadata_collector.h"
#include "mem_collector.h"
#include "network_collector.h"
#include "pod_metric_aggregator.h"
#include "pressure_collector.h"
#include "process_collector.h"
#include "tcp_collector.h"

#include <chrono>
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

struct NodeMetric {
    std::uint64_t timestamp_unix_ms = 0;
    std::string hostname;
    std::uint64_t logical_cpu_count = 0;
    double cpu_usage_percent = 0.0;
    double memory_usage_percent = 0.0;
    std::uint64_t memory_total_kb = 0;
    std::uint64_t memory_available_kb = 0;
    std::uint64_t swap_total_kb = 0;
    std::uint64_t swap_free_kb = 0;
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
    PressureSample pressure;
};

struct TelemetrySnapshot {
    NodeMetric node;
    std::vector<ProcessMetric> processes;
    std::vector<ProcessMetric> top_cpu_processes;
    std::vector<ProcessMetric> top_memory_processes;
    std::vector<ContainerMetric> containers;
    std::vector<PodMetric> pods;
    std::vector<DeploymentMetric> deployments;
    std::vector<KubernetesEvent> kubernetes_events;
    CgroupCollectionSample cgroups;
    bool kubernetes_metadata_available = false;
};

class TelemetryCollector {
public:
    TelemetryCollector();
    TelemetrySnapshot collect();

private:
    std::string hostname_;
    CpuCollector cpu_collector_;
    MemoryCollector memory_collector_;
    NetworkCollector network_collector_;
    TcpCollector tcp_collector_;
    DiskCollector disk_collector_;
    ProcessCollector process_collector_;
    CgroupCollector cgroup_collector_;
    PressureCollector pressure_collector_;
    KubernetesMetadataCollector kubernetes_metadata_collector_;
    CpuSample previous_cpu_sample_;
    NetworkSample previous_network_sample_;
    TcpSample previous_tcp_sample_;
    DiskSample previous_disk_sample_;
    ProcessCollectionSample previous_process_sample_;
    CgroupCollectionSample previous_cgroup_sample_;
    std::chrono::steady_clock::time_point previous_cgroup_sample_time_;
    KubernetesMetadataSnapshot kubernetes_metadata_;
    bool has_kubernetes_metadata_ = false;
    bool has_attempted_kubernetes_metadata_refresh_ = false;
    std::chrono::steady_clock::time_point last_kubernetes_metadata_refresh_;
};

#endif
