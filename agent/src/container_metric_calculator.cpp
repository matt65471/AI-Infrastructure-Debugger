#include "container_metric_calculator.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::uint64_t positive_delta(std::uint64_t previous, std::uint64_t current) {
    return current >= previous ? current - previous : 0;
}

std::unordered_map<std::string, const CgroupSample*> index_by_container_id(
    const CgroupCollectionSample& sample) {
    std::unordered_map<std::string, const CgroupSample*> index;
    for (const CgroupSample& cgroup : sample.kubernetes_cgroups) {
        if (!cgroup.container_id.empty()) {
            index[cgroup.container_id] = &cgroup;
        }
    }
    return index;
}

}  // namespace

std::vector<ContainerMetric> calculate_container_metrics(
    const CgroupCollectionSample& previous,
    const CgroupCollectionSample& current,
    std::uint64_t elapsed_usec,
    std::uint64_t timestamp_unix_ms) {
    const std::unordered_map<std::string, const CgroupSample*> previous_index =
        index_by_container_id(previous);
    std::vector<ContainerMetric> metrics;

    for (const CgroupSample& current_cgroup : current.kubernetes_cgroups) {
        if (current_cgroup.container_id.empty()) {
            continue;
        }

        ContainerMetric metric;
        metric.timestamp_unix_ms = timestamp_unix_ms;
        metric.container_id = current_cgroup.container_id;
        metric.cgroup_path = current_cgroup.path;
        metric.cpu_usage_usec = current_cgroup.cpu.usage_usec;
        metric.memory_current_bytes = current_cgroup.memory_current_bytes;
        metric.memory_max_bytes = current_cgroup.memory_max_bytes;
        metric.memory_is_unlimited = current_cgroup.memory_is_unlimited;
        metric.process_ids = current_cgroup.process_ids;

        if (!metric.memory_is_unlimited && metric.memory_max_bytes > 0) {
            metric.memory_usage_percent_available = true;
            metric.memory_usage_percent =
                100.0 * static_cast<double>(metric.memory_current_bytes) /
                static_cast<double>(metric.memory_max_bytes);
        }

        const auto previous_iterator =
            previous_index.find(current_cgroup.container_id);
        if (previous_iterator != previous_index.end()) {
            const CgroupSample& previous_cgroup = *previous_iterator->second;
            if (elapsed_usec > 0 &&
                current_cgroup.cpu.usage_usec >= previous_cgroup.cpu.usage_usec) {
                metric.cpu_usage_available = true;
                const std::uint64_t cpu_delta = positive_delta(
                    previous_cgroup.cpu.usage_usec,
                    current_cgroup.cpu.usage_usec);
                metric.cpu_usage_percent =
                    100.0 * static_cast<double>(cpu_delta) /
                    static_cast<double>(elapsed_usec);
            }

            metric.throttled_periods_delta = positive_delta(
                previous_cgroup.cpu.throttled_periods,
                current_cgroup.cpu.throttled_periods);
            metric.throttled_usec_delta = positive_delta(
                previous_cgroup.cpu.throttled_usec,
                current_cgroup.cpu.throttled_usec);
            metric.oom_delta = positive_delta(
                previous_cgroup.memory_events.oom,
                current_cgroup.memory_events.oom);
            metric.oom_kill_delta = positive_delta(
                previous_cgroup.memory_events.oom_kill,
                current_cgroup.memory_events.oom_kill);
        }

        metrics.push_back(std::move(metric));
    }

    return metrics;
}
