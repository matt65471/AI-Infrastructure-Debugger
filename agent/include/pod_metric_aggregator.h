#ifndef POD_METRIC_AGGREGATOR_H
#define POD_METRIC_AGGREGATOR_H

#include "container_metric_calculator.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PodMetric {
    std::uint64_t timestamp_unix_ms = 0;
    std::string node_name;
    std::string namespace_name;
    std::string pod_name;
    std::string pod_uid;
    std::string pod_phase;
    std::string pod_qos_class;
    bool pod_ready = false;
    bool pod_scheduled = false;
    bool pod_initialized = false;
    std::string workload_kind;
    std::string workload_name;
    std::string workload_uid;

    bool cpu_usage_available = false;
    double cpu_usage_percent = 0.0;
    std::uint64_t cpu_usage_usec = 0;
    bool cpu_limit_available = false;
    bool cpu_is_unlimited = false;
    double cpu_limit_cores = 0.0;
    std::uint64_t throttled_periods_delta = 0;
    std::uint64_t throttled_usec_delta = 0;

    std::uint64_t memory_current_bytes = 0;
    std::uint64_t memory_max_bytes = 0;
    bool memory_limit_available = false;
    bool memory_is_unlimited = false;
    bool memory_usage_percent_available = false;
    double memory_usage_percent = 0.0;
    std::uint64_t memory_high_delta = 0;
    std::uint64_t memory_max_delta = 0;
    std::uint64_t oom_delta = 0;
    std::uint64_t oom_kill_delta = 0;

    bool cpu_request_available = false;
    double cpu_request_cores = 0.0;
    bool kubernetes_cpu_limit_available = false;
    double kubernetes_cpu_limit_cores = 0.0;
    bool memory_request_available = false;
    std::uint64_t memory_request_bytes = 0;
    bool kubernetes_memory_limit_available = false;
    std::uint64_t kubernetes_memory_limit_bytes = 0;

    bool all_containers_ready = false;
    std::uint64_t restart_count = 0;
    std::size_t container_count = 0;
    std::vector<std::string> container_ids;
    std::vector<std::string> container_names;
    std::vector<int> process_ids;
    std::vector<std::string> lifecycle_reasons;
};

std::vector<PodMetric> aggregate_pod_metrics(
    const std::vector<ContainerMetric>& containers);

#endif
