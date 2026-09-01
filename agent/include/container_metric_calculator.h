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
    std::uint64_t throttled_periods_delta = 0;
    std::uint64_t throttled_usec_delta = 0;

    std::uint64_t memory_current_bytes = 0;
    std::uint64_t memory_max_bytes = 0;
    bool memory_is_unlimited = false;
    bool memory_usage_percent_available = false;
    double memory_usage_percent = 0.0;
    std::uint64_t oom_delta = 0;
    std::uint64_t oom_kill_delta = 0;

    std::vector<int> process_ids;
};

std::vector<ContainerMetric> calculate_container_metrics(
    const CgroupCollectionSample& previous,
    const CgroupCollectionSample& current,
    std::uint64_t elapsed_usec,
    std::uint64_t timestamp_unix_ms);

#endif
