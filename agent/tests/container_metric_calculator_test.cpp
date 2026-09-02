#include "container_metric_calculator.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

CgroupSample make_cgroup(const std::string& container_id) {
    CgroupSample sample;
    sample.container_id = container_id;
    sample.path = "/kubepods/cri-containerd-" + container_id + ".scope";
    return sample;
}

}  // namespace

int main() {
    const std::string existing_id(64, 'a');
    const std::string new_id(64, 'b');
    const std::string disappeared_id(64, 'c');

    CgroupSample previous_existing = make_cgroup(existing_id);
    previous_existing.cpu.usage_usec = 1'000'000;
    previous_existing.cpu.throttled_periods = 2;
    previous_existing.cpu.throttled_usec = 50;
    previous_existing.memory_events.oom = 1;
    previous_existing.memory_events.oom_kill = 0;
    previous_existing.memory_events.high = 4;
    previous_existing.memory_events.max = 6;

    CgroupSample previous_disappeared = make_cgroup(disappeared_id);
    previous_disappeared.cpu.usage_usec = 400'000;

    CgroupCollectionSample previous;
    previous.cgroup_v2_available = true;
    previous.kubernetes_cgroups = {previous_existing, previous_disappeared};

    CgroupSample current_existing = make_cgroup(existing_id);
    current_existing.cpu.usage_usec = 1'800'000;
    current_existing.cpu.throttled_periods = 5;
    current_existing.cpu.throttled_usec = 125;
    current_existing.memory_current_bytes = 4096;
    current_existing.memory_max_bytes = 8192;
    current_existing.cpu_limit_available = true;
    current_existing.cpu_quota_usec = 50'000;
    current_existing.cpu_period_usec = 100'000;
    current_existing.memory_events.high = 7;
    current_existing.memory_events.max = 10;
    current_existing.memory_events.oom = 3;
    current_existing.memory_events.oom_kill = 1;
    current_existing.process_ids = {101, 102};

    CgroupSample current_new = make_cgroup(new_id);
    current_new.cpu.usage_usec = 250'000;
    current_new.memory_current_bytes = 2048;
    current_new.memory_is_unlimited = true;
    current_new.process_ids = {202};

    CgroupSample pod_parent;
    pod_parent.path = "/kubepods/pod-parent";
    pod_parent.cpu.usage_usec = 2'000'000;

    CgroupCollectionSample current;
    current.cgroup_v2_available = true;
    current.kubernetes_cgroups = {current_existing, current_new, pod_parent};

    const std::vector<ContainerMetric> metrics = calculate_container_metrics(
        previous, current, 1'000'000, 123456789);

    bool passed = true;
    passed &= expect(metrics.size() == 2,
                     "keeps active container leaf cgroups only");
    if (metrics.size() == 2) {
        const ContainerMetric& existing = metrics[0];
        passed &= expect(existing.timestamp_unix_ms == 123456789,
                         "attaches collection timestamp");
        passed &= expect(existing.cpu_usage_available,
                         "calculates CPU after two samples");
        passed &= expect(std::abs(existing.cpu_usage_percent - 80.0) < 0.001,
                         "calculates CPU percentage from elapsed time");
        passed &= expect(existing.throttled_periods_delta == 3,
                         "calculates throttled period delta");
        passed &= expect(existing.throttled_usec_delta == 75,
                         "calculates throttled time delta");
        passed &= expect(existing.cpu_limit_available &&
                             std::abs(existing.cpu_limit_cores - 0.5) < 0.001,
                         "converts the cgroup CPU quota to cores");
        passed &= expect(existing.memory_usage_percent_available,
                         "calculates limited memory utilization");
        passed &= expect(
            std::abs(existing.memory_usage_percent - 50.0) < 0.001,
            "calculates memory percentage");
        passed &= expect(existing.oom_delta == 2,
                         "calculates OOM event delta");
        passed &= expect(existing.memory_high_delta == 3 &&
                             existing.memory_max_delta == 4,
                         "calculates memory pressure event deltas");
        passed &= expect(existing.oom_kill_delta == 1,
                         "calculates OOM kill delta");
        passed &= expect(existing.process_ids == std::vector<int>({101, 102}),
                         "preserves member PIDs");

        const ContainerMetric& newly_seen = metrics[1];
        passed &= expect(!newly_seen.cpu_usage_available,
                         "does not invent CPU rate for a new container");
        passed &= expect(!newly_seen.memory_usage_percent_available,
                         "does not calculate percentage without a memory limit");
        passed &= expect(newly_seen.memory_is_unlimited,
                         "preserves unlimited memory state");
    }

    return passed ? 0 : 1;
}
