#ifndef DEPLOYMENT_METRIC_AGGREGATOR_H
#define DEPLOYMENT_METRIC_AGGREGATOR_H

#include "kubernetes_metadata_collector.h"
#include "pod_metric_aggregator.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct DeploymentMetric {
    std::uint64_t timestamp_unix_ms = 0;
    std::string namespace_name;
    std::string deployment_name;
    std::string deployment_uid;
    std::uint64_t generation = 0;
    std::uint64_t observed_generation = 0;
    std::uint64_t desired_replicas = 0;
    std::uint64_t ready_replicas = 0;
    std::uint64_t available_replicas = 0;
    std::uint64_t unavailable_replicas = 0;
    std::uint64_t updated_replicas = 0;

    std::size_t observed_pod_count = 0;
    bool cpu_usage_available = false;
    double cpu_usage_percent = 0.0;
    std::uint64_t cpu_usage_usec = 0;
    std::uint64_t memory_current_bytes = 0;
    std::uint64_t throttled_periods_delta = 0;
    std::uint64_t throttled_usec_delta = 0;
    std::uint64_t memory_high_delta = 0;
    std::uint64_t memory_max_delta = 0;
    std::uint64_t oom_delta = 0;
    std::uint64_t oom_kill_delta = 0;
    std::uint64_t restart_count = 0;
    bool cpu_request_available = false;
    double cpu_request_cores = 0.0;
    bool cpu_limit_available = false;
    double cpu_limit_cores = 0.0;
    bool memory_request_available = false;
    std::uint64_t memory_request_bytes = 0;
    bool memory_limit_available = false;
    std::uint64_t memory_limit_bytes = 0;
    std::vector<std::string> pod_names;
};

std::vector<DeploymentMetric> aggregate_deployment_metrics(
    const std::vector<PodMetric>& pods,
    const KubernetesMetadataSnapshot& metadata,
    std::uint64_t timestamp_unix_ms);

#endif
