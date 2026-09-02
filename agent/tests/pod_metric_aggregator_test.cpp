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
    container.pod_qos_class = "Burstable";
    container.pod_ready = true;
    container.pod_scheduled = true;
    container.pod_initialized = true;
    container.container_name = container_name;
    container.workload_kind = "Deployment";
    container.workload_name = "payment";
    container.workload_uid = "deployment-uid";
    return container;
}

}  // namespace

int main() {
    ContainerMetric application =
        make_container(std::string(64, 'a'), "payment", "pod-a", "payment-a");
    application.cpu_usage_available = true;
    application.cpu_usage_percent = 20.0;
    application.cpu_usage_usec = 1'000;
    application.cpu_limit_available = true;
    application.cpu_limit_cores = 0.5;
    application.throttled_periods_delta = 2;
    application.throttled_usec_delta = 50;
    application.memory_current_bytes = 40;
    application.memory_max_bytes = 100;
    application.memory_high_delta = 2;
    application.memory_max_delta = 1;
    application.oom_delta = 1;
    application.container_ready = true;
    application.restart_count = 1;
    application.cpu_request_available = true;
    application.cpu_request_cores = 0.25;
    application.kubernetes_cpu_limit_available = true;
    application.kubernetes_cpu_limit_cores = 0.5;
    application.memory_request_available = true;
    application.memory_request_bytes = 32;
    application.kubernetes_memory_limit_available = true;
    application.kubernetes_memory_limit_bytes = 100;
    application.last_termination_reason = "OOMKilled";
    application.process_ids = {10, 11};

    ContainerMetric sidecar =
        make_container(std::string(64, 'b'), "logger", "pod-a", "payment-a");
    sidecar.timestamp_unix_ms = 1001;
    sidecar.cpu_usage_available = true;
    sidecar.cpu_usage_percent = 5.0;
    sidecar.cpu_usage_usec = 500;
    sidecar.cpu_limit_available = true;
    sidecar.cpu_limit_cores = 0.25;
    sidecar.throttled_periods_delta = 1;
    sidecar.throttled_usec_delta = 25;
    sidecar.memory_current_bytes = 10;
    sidecar.memory_max_bytes = 100;
    sidecar.memory_high_delta = 3;
    sidecar.memory_max_delta = 2;
    sidecar.oom_kill_delta = 1;
    sidecar.container_ready = false;
    sidecar.restart_count = 2;
    sidecar.cpu_request_available = true;
    sidecar.cpu_request_cores = 0.05;
    sidecar.kubernetes_cpu_limit_available = true;
    sidecar.kubernetes_cpu_limit_cores = 0.1;
    sidecar.memory_request_available = true;
    sidecar.memory_request_bytes = 16;
    sidecar.kubernetes_memory_limit_available = true;
    sidecar.kubernetes_memory_limit_bytes = 50;
    sidecar.state_reason = "CrashLoopBackOff";
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
        passed &= expect(payment.cpu_limit_available &&
                             std::abs(payment.cpu_limit_cores - 0.75) < 0.001,
                         "sums cgroup CPU quotas");
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
        passed &= expect(payment.memory_high_delta == 5 &&
                             payment.memory_max_delta == 3,
                         "sums cgroup memory pressure deltas");
        passed &= expect(payment.cpu_request_available &&
                             std::abs(payment.cpu_request_cores - 0.30) <
                                 0.001 &&
                             payment.kubernetes_cpu_limit_available &&
                             std::abs(payment.kubernetes_cpu_limit_cores -
                                      0.60) < 0.001,
                         "sums configured Kubernetes CPU resources");
        passed &= expect(payment.memory_request_available &&
                             payment.memory_request_bytes == 48 &&
                             payment.kubernetes_memory_limit_available &&
                             payment.kubernetes_memory_limit_bytes == 150,
                         "sums configured Kubernetes memory resources");
        passed &= expect(payment.pod_qos_class == "Burstable" &&
                             payment.pod_ready && payment.pod_scheduled &&
                             payment.pod_initialized,
                         "preserves pod scheduling and QoS state");
        passed &= expect(payment.lifecycle_reasons ==
                             std::vector<std::string>({
                                 "logger:CrashLoopBackOff",
                                 "payment:last=OOMKilled"}),
                         "collects and sorts lifecycle failure reasons");
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
