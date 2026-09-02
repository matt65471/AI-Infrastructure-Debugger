#ifndef CONTAINER_METRIC_CALCULATOR_H
#define CONTAINER_METRIC_CALCULATOR_H

#include "cgroup_collector.h"

#include <cstdint>
#include <string>
#include <vector>

struct ContainerMetric {
    std::uint64_t timestamp_unix_ms = 0;
    std::string container_id;
    std::string cgroup_path;

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
    bool memory_is_unlimited = false;
    bool memory_usage_percent_available = false;
    double memory_usage_percent = 0.0;
    std::uint64_t memory_high_delta = 0;
    std::uint64_t memory_max_delta = 0;
    std::uint64_t oom_delta = 0;
    std::uint64_t oom_kill_delta = 0;

    std::vector<int> process_ids;

    bool kubernetes_identity_available = false;
    std::string namespace_name;
    std::string pod_name;
    std::string pod_uid;
    std::string node_name;
    std::string container_name;
    std::string image;
    std::string pod_phase;
    std::string pod_qos_class;
    bool pod_ready = false;
    bool pod_scheduled = false;
    bool pod_initialized = false;
    bool container_ready = false;
    std::uint64_t restart_count = 0;
    std::string container_state;
    std::string state_reason;
    std::int64_t exit_code = 0;
    std::string last_termination_reason;
    std::int64_t last_exit_code = 0;
    std::string started_at;
    std::string finished_at;
    std::string last_finished_at;
    bool cpu_request_available = false;
    double cpu_request_cores = 0.0;
    bool kubernetes_cpu_limit_available = false;
    double kubernetes_cpu_limit_cores = 0.0;
    bool memory_request_available = false;
    std::uint64_t memory_request_bytes = 0;
    bool kubernetes_memory_limit_available = false;
    std::uint64_t kubernetes_memory_limit_bytes = 0;
    std::string workload_kind;
    std::string workload_name;
    std::string workload_uid;
};

std::vector<ContainerMetric> calculate_container_metrics(
    const CgroupCollectionSample& previous,
    const CgroupCollectionSample& current,
    std::uint64_t elapsed_usec,
    std::uint64_t timestamp_unix_ms);

#endif
