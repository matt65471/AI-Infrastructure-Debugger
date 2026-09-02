#include "pod_metric_aggregator.h"

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

ContainerMetric make_container(const std::string& container_id,
                               const std::string& container_name,
                               const std::string& pod_uid,
                               const std::string& pod_name) {
    ContainerMetric container;
    container.timestamp_unix_ms = 1000;
    container.container_id = container_id;
    container.kubernetes_identity_available = true;
    container.node_name = "ubuntu-vm";
    container.namespace_name = "infrastructure-demo";
    container.pod_uid = pod_uid;
    container.pod_name = pod_name;
    container.pod_phase = "Running";
    container.container_name = container_name;
    container.workload_kind = "Deployment";
    container.workload_name = "payment";
    return container;
}

}  // namespace

int main() {
    ContainerMetric application =
        make_container(std::string(64, 'a'), "payment", "pod-a", "payment-a");
    application.cpu_usage_available = true;
    application.cpu_usage_percent = 20.0;
    application.cpu_usage_usec = 1'000;
    application.throttled_periods_delta = 2;
    application.throttled_usec_delta = 50;
    application.memory_current_bytes = 40;
    application.memory_max_bytes = 100;
    application.oom_delta = 1;
    application.container_ready = true;
    application.restart_count = 1;
    application.process_ids = {10, 11};

    ContainerMetric sidecar =
        make_container(std::string(64, 'b'), "logger", "pod-a", "payment-a");
    sidecar.timestamp_unix_ms = 1001;
    sidecar.cpu_usage_available = true;
    sidecar.cpu_usage_percent = 5.0;
    sidecar.cpu_usage_usec = 500;
    sidecar.throttled_periods_delta = 1;
    sidecar.throttled_usec_delta = 25;
    sidecar.memory_current_bytes = 10;
    sidecar.memory_max_bytes = 100;
    sidecar.oom_kill_delta = 1;
    sidecar.container_ready = false;
    sidecar.restart_count = 2;
    sidecar.process_ids = {11, 12};

    ContainerMetric unlimited =
        make_container(std::string(64, 'c'), "worker", "pod-b", "worker-b");
    unlimited.cpu_usage_available = false;
    unlimited.cpu_usage_percent = 0.0;
    unlimited.memory_current_bytes = 30;
    unlimited.memory_is_unlimited = true;
    unlimited.container_ready = true;

    ContainerMetric unmatched;
    unmatched.container_id = std::string(64, 'd');
    unmatched.cpu_usage_percent = 99.0;

    ContainerMetric missing_uid =
        make_container(std::string(64, 'e'), "unknown", "", "unknown");

    const std::vector<PodMetric> pods = aggregate_pod_metrics(
        {application, sidecar, unlimited, unmatched, missing_uid});

    bool passed = true;
    passed &= expect(pods.size() == 2,
                     "groups matched containers with pod UIDs only");
    if (pods.size() == 2) {
        const PodMetric& payment = pods[0];
        passed &= expect(payment.pod_uid == "pod-a" &&
                             payment.pod_name == "payment-a",
                         "preserves pod identity");
        passed &= expect(payment.container_count == 2,
                         "counts containers in the pod");
        passed &= expect(payment.cpu_usage_available,
                         "reports complete CPU aggregation");
        passed &= expect(std::abs(payment.cpu_usage_percent - 25.0) < 0.001,
                         "sums container CPU usage");
        passed &= expect(payment.cpu_usage_usec == 1'500,
                         "sums cumulative container CPU time");
        passed &= expect(payment.throttled_periods_delta == 3 &&
                             payment.throttled_usec_delta == 75,
                         "sums throttling deltas");
        passed &= expect(payment.memory_current_bytes == 50 &&
                             payment.memory_max_bytes == 200,
                         "sums current memory and finite limits");
        passed &= expect(payment.memory_usage_percent_available &&
                             std::abs(payment.memory_usage_percent - 25.0) <
                                 0.001,
                         "calculates pod memory utilization");
        passed &= expect(payment.oom_delta == 1 &&
                             payment.oom_kill_delta == 1,
                         "sums OOM deltas");
        passed &= expect(!payment.all_containers_ready,
                         "requires every container to be ready");
        passed &= expect(payment.restart_count == 3,
                         "sums container restart counts");
        passed &= expect(payment.process_ids == std::vector<int>({10, 11, 12}),
                         "deduplicates and sorts host PIDs");

        const PodMetric& worker = pods[1];
        passed &= expect(!worker.cpu_usage_available,
                         "preserves unavailable CPU for a new container");
        passed &= expect(worker.memory_is_unlimited &&
                             !worker.memory_limit_available &&
                             !worker.memory_usage_percent_available,
                         "preserves unlimited pod memory state");
    }

    return passed ? 0 : 1;
}
