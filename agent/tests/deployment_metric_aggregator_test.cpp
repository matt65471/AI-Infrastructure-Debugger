#include "deployment_metric_aggregator.h"

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

PodMetric make_pod(const std::string& name, double cpu, std::uint64_t memory) {
    PodMetric pod;
    pod.timestamp_unix_ms = 1000;
    pod.namespace_name = "infrastructure-demo";
    pod.pod_name = name;
    pod.pod_uid = name + "-uid";
    pod.workload_kind = "Deployment";
    pod.workload_name = "payment";
    pod.workload_uid = "payment-deployment-uid";
    pod.cpu_usage_available = true;
    pod.cpu_usage_percent = cpu;
    pod.cpu_usage_usec = 50;
    pod.memory_current_bytes = memory;
    pod.throttled_periods_delta = 3;
    pod.throttled_usec_delta = 10;
    pod.memory_high_delta = 4;
    pod.memory_max_delta = 5;
    pod.oom_delta = 6;
    pod.oom_kill_delta = 1;
    pod.restart_count = 2;
    pod.cpu_request_available = true;
    pod.cpu_request_cores = 0.25;
    pod.kubernetes_cpu_limit_available = true;
    pod.kubernetes_cpu_limit_cores = 0.5;
    pod.memory_request_available = true;
    pod.memory_request_bytes = 64;
    pod.kubernetes_memory_limit_available = true;
    pod.kubernetes_memory_limit_bytes = 128;
    return pod;
}

}  // namespace

int main() {
    KubernetesMetadataSnapshot metadata;
    metadata.available = true;

    KubernetesDeploymentInfo payment;
    payment.namespace_name = "infrastructure-demo";
    payment.deployment_name = "payment";
    payment.deployment_uid = "payment-deployment-uid";
    payment.generation = 5;
    payment.observed_generation = 5;
    payment.desired_replicas = 3;
    payment.ready_replicas = 2;
    payment.available_replicas = 2;
    payment.unavailable_replicas = 1;
    payment.updated_replicas = 3;
    metadata.deployments_by_key["infrastructure-demo\npayment"] = payment;

    KubernetesDeploymentInfo checkout;
    checkout.namespace_name = "infrastructure-demo";
    checkout.deployment_name = "checkout";
    checkout.deployment_uid = "checkout-deployment-uid";
    checkout.desired_replicas = 1;
    metadata.deployments_by_key["infrastructure-demo\ncheckout"] = checkout;

    const std::vector<DeploymentMetric> deployments =
        aggregate_deployment_metrics(
            {make_pod("payment-b", 20.0, 100),
             make_pod("payment-a", 30.0, 200)},
            metadata, 2000);

    bool passed = true;
    passed &= expect(deployments.size() == 2,
                     "keeps deployments even when no container cgroup is visible");
    if (deployments.size() == 2) {
        const DeploymentMetric& empty = deployments[0];
        passed &= expect(empty.deployment_name == "checkout" &&
                             empty.observed_pod_count == 0 &&
                             !empty.cpu_usage_available,
                         "represents a deployment with no observed running pods");

        const DeploymentMetric& aggregate = deployments[1];
        passed &= expect(aggregate.deployment_name == "payment" &&
                             aggregate.desired_replicas == 3 &&
                             aggregate.ready_replicas == 2 &&
                             aggregate.unavailable_replicas == 1,
                         "combines Kubernetes deployment health with pod metrics");
        passed &= expect(aggregate.observed_pod_count == 2 &&
                             aggregate.cpu_usage_available &&
                             std::abs(aggregate.cpu_usage_percent - 50.0) <
                                 0.001 &&
                             aggregate.cpu_usage_usec == 100 &&
                             aggregate.memory_current_bytes == 300,
                         "sums observed pod CPU and memory");
        passed &= expect(aggregate.throttled_periods_delta == 6 &&
                             aggregate.throttled_usec_delta == 20 &&
                             aggregate.memory_high_delta == 8 &&
                             aggregate.memory_max_delta == 10 &&
                             aggregate.oom_delta == 12 &&
                             aggregate.oom_kill_delta == 2 &&
                             aggregate.restart_count == 4,
                         "sums pod fault signals");
        passed &= expect(aggregate.cpu_request_available &&
                             std::abs(aggregate.cpu_request_cores - 0.5) <
                                 0.001 &&
                             aggregate.cpu_limit_available &&
                             std::abs(aggregate.cpu_limit_cores - 1.0) <
                                 0.001,
                         "sums configured CPU resources");
        passed &= expect(aggregate.memory_request_available &&
                             aggregate.memory_request_bytes == 128 &&
                             aggregate.memory_limit_available &&
                             aggregate.memory_limit_bytes == 256,
                         "sums configured memory resources");
        passed &= expect(aggregate.pod_names ==
                             std::vector<std::string>({"payment-a", "payment-b"}),
                         "sorts contributing pod names");
    }

    return passed ? 0 : 1;
}
