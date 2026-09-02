#include "pod_metric_aggregator.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

void initialize_identity(PodMetric& pod, const ContainerMetric& container) {
    pod.timestamp_unix_ms = container.timestamp_unix_ms;
    pod.node_name = container.node_name;
    pod.namespace_name = container.namespace_name;
    pod.pod_name = container.pod_name;
    pod.pod_uid = container.pod_uid;
    pod.pod_phase = container.pod_phase;
    pod.workload_kind = container.workload_kind;
    pod.workload_name = container.workload_name;
    pod.cpu_usage_available = true;
    pod.memory_limit_available = true;
    pod.all_containers_ready = true;
}

void add_container(PodMetric& pod, const ContainerMetric& container) {
    pod.timestamp_unix_ms =
        std::max(pod.timestamp_unix_ms, container.timestamp_unix_ms);
    pod.cpu_usage_available =
        pod.cpu_usage_available && container.cpu_usage_available;
    pod.cpu_usage_percent += container.cpu_usage_percent;
    pod.cpu_usage_usec += container.cpu_usage_usec;
    pod.throttled_periods_delta += container.throttled_periods_delta;
    pod.throttled_usec_delta += container.throttled_usec_delta;

    pod.memory_current_bytes += container.memory_current_bytes;
    if (container.memory_is_unlimited) {
        pod.memory_is_unlimited = true;
        pod.memory_limit_available = false;
    } else if (container.memory_max_bytes == 0) {
        pod.memory_limit_available = false;
    } else if (pod.memory_limit_available) {
        pod.memory_max_bytes += container.memory_max_bytes;
    }

    pod.oom_delta += container.oom_delta;
    pod.oom_kill_delta += container.oom_kill_delta;
    pod.all_containers_ready =
        pod.all_containers_ready && container.container_ready;
    pod.restart_count += container.restart_count;
    ++pod.container_count;
    pod.container_ids.push_back(container.container_id);
    pod.container_names.push_back(container.container_name);
    pod.process_ids.insert(pod.process_ids.end(),
                           container.process_ids.begin(),
                           container.process_ids.end());
}

void finalize(PodMetric& pod) {
    if (pod.memory_limit_available && pod.memory_max_bytes > 0) {
        pod.memory_usage_percent_available = true;
        pod.memory_usage_percent =
            100.0 * static_cast<double>(pod.memory_current_bytes) /
            static_cast<double>(pod.memory_max_bytes);
    }

    std::sort(pod.container_ids.begin(), pod.container_ids.end());
    std::sort(pod.container_names.begin(), pod.container_names.end());
    std::sort(pod.process_ids.begin(), pod.process_ids.end());
    pod.process_ids.erase(
        std::unique(pod.process_ids.begin(), pod.process_ids.end()),
        pod.process_ids.end());
}

}  // namespace

std::vector<PodMetric> aggregate_pod_metrics(
    const std::vector<ContainerMetric>& containers) {
    std::map<std::string, PodMetric> pods_by_uid;

    for (const ContainerMetric& container : containers) {
        if (!container.kubernetes_identity_available ||
            container.pod_uid.empty()) {
            continue;
        }

        auto [iterator, inserted] =
            pods_by_uid.try_emplace(container.pod_uid);
        PodMetric& pod = iterator->second;
        if (inserted) {
            initialize_identity(pod, container);
        }
        add_container(pod, container);
    }

    std::vector<PodMetric> metrics;
    metrics.reserve(pods_by_uid.size());
    for (auto& [pod_uid, pod] : pods_by_uid) {
        static_cast<void>(pod_uid);
        finalize(pod);
        metrics.push_back(std::move(pod));
    }
    return metrics;
}
