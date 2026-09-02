#include "deployment_metric_aggregator.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string deployment_key(const std::string& namespace_name,
                           const std::string& deployment_name) {
    return namespace_name + "\n" + deployment_name;
}

DeploymentMetric from_kubernetes(const KubernetesDeploymentInfo& info,
                                 std::uint64_t timestamp_unix_ms) {
    DeploymentMetric metric;
    metric.timestamp_unix_ms = timestamp_unix_ms;
    metric.namespace_name = info.namespace_name;
    metric.deployment_name = info.deployment_name;
    metric.deployment_uid = info.deployment_uid;
    metric.generation = info.generation;
    metric.observed_generation = info.observed_generation;
    metric.desired_replicas = info.desired_replicas;
    metric.ready_replicas = info.ready_replicas;
    metric.available_replicas = info.available_replicas;
    metric.unavailable_replicas = info.unavailable_replicas;
    metric.updated_replicas = info.updated_replicas;
    metric.cpu_usage_available = true;
    metric.cpu_request_available = true;
    metric.cpu_limit_available = true;
    metric.memory_request_available = true;
    metric.memory_limit_available = true;
    return metric;
}

void add_pod(DeploymentMetric& deployment, const PodMetric& pod) {
    if (deployment.namespace_name.empty()) {
        deployment.timestamp_unix_ms = pod.timestamp_unix_ms;
        deployment.namespace_name = pod.namespace_name;
        deployment.deployment_name = pod.workload_name;
        deployment.deployment_uid = pod.workload_uid;
        deployment.cpu_usage_available = true;
        deployment.cpu_request_available = true;
        deployment.cpu_limit_available = true;
        deployment.memory_request_available = true;
        deployment.memory_limit_available = true;
    }

    ++deployment.observed_pod_count;
    deployment.pod_names.push_back(pod.pod_name);
    deployment.cpu_usage_available =
        deployment.cpu_usage_available && pod.cpu_usage_available;
    deployment.cpu_usage_percent += pod.cpu_usage_percent;
    deployment.cpu_usage_usec += pod.cpu_usage_usec;
    deployment.memory_current_bytes += pod.memory_current_bytes;
    deployment.throttled_periods_delta += pod.throttled_periods_delta;
    deployment.throttled_usec_delta += pod.throttled_usec_delta;
    deployment.memory_high_delta += pod.memory_high_delta;
    deployment.memory_max_delta += pod.memory_max_delta;
    deployment.oom_delta += pod.oom_delta;
    deployment.oom_kill_delta += pod.oom_kill_delta;
    deployment.restart_count += pod.restart_count;

    deployment.cpu_request_available =
        deployment.cpu_request_available && pod.cpu_request_available;
    deployment.cpu_request_cores += pod.cpu_request_cores;
    deployment.cpu_limit_available =
        deployment.cpu_limit_available &&
        pod.kubernetes_cpu_limit_available;
    deployment.cpu_limit_cores += pod.kubernetes_cpu_limit_cores;
    deployment.memory_request_available =
        deployment.memory_request_available && pod.memory_request_available;
    deployment.memory_request_bytes += pod.memory_request_bytes;
    deployment.memory_limit_available =
        deployment.memory_limit_available &&
        pod.kubernetes_memory_limit_available;
    deployment.memory_limit_bytes += pod.kubernetes_memory_limit_bytes;
}

}  // namespace

std::vector<DeploymentMetric> aggregate_deployment_metrics(
    const std::vector<PodMetric>& pods,
    const KubernetesMetadataSnapshot& metadata,
    std::uint64_t timestamp_unix_ms) {
    std::map<std::string, DeploymentMetric> deployments;
    for (const auto& [key, info] : metadata.deployments_by_key) {
        deployments[key] = from_kubernetes(info, timestamp_unix_ms);
    }

    for (const PodMetric& pod : pods) {
        if (pod.workload_kind != "Deployment" || pod.workload_name.empty()) {
            continue;
        }
        add_pod(deployments[deployment_key(pod.namespace_name,
                                           pod.workload_name)],
                pod);
    }

    std::vector<DeploymentMetric> metrics;
    metrics.reserve(deployments.size());
    for (auto& [key, deployment] : deployments) {
        static_cast<void>(key);
        std::sort(deployment.pod_names.begin(), deployment.pod_names.end());
        if (deployment.observed_pod_count == 0) {
            deployment.cpu_usage_available = false;
            deployment.cpu_request_available = false;
            deployment.cpu_limit_available = false;
            deployment.memory_request_available = false;
            deployment.memory_limit_available = false;
        }
        metrics.push_back(std::move(deployment));
    }
    return metrics;
}
