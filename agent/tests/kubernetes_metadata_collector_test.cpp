#include "kubernetes_metadata_collector.h"

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

}  // namespace

int main() {
    const std::string payment_id(64, 'a');
    const std::string sidecar_id(64, 'b');
    const std::string unmatched_id(64, 'c');
    const std::string pod_output =
        "infrastructure-demo\tpayment-abc\tpod-uid-123\tubuntu-vm\tRunning\t"
        "ReplicaSet\tpayment-abc\tcontainerd://" +
        payment_id +
        "|payment|infrastructure-debugger/payment:v1|2|true||||OOMKilled|137|"
        "2026-09-02T10:00:00Z||2026-09-02T09:59:00Z;containerd://" +
        sidecar_id +
        "|logger|example/logger:v1|3|false|CrashLoopBackOff|||||||;\t"
        "Burstable\tTrue\tTrue\tTrue\t"
        "payment|250m|1|128Mi|512Mi;logger|50m|100m|32Mi|64Mi;\n"
        "infrastructure-demo\tpending-pod\tpod-uid-456\tubuntu-vm\tPending\t"
        "ReplicaSet\tpending-abc\t\n";
    const std::string replica_set_output =
        "infrastructure-demo\tpayment-abc\tDeployment\tpayment\t"
        "deployment-uid-123\n";
    const std::string deployment_output =
        "infrastructure-demo\tpayment\tdeployment-uid-123\t4\t4\t3\t2\t2\t1\t3\n";
    const std::string event_output =
        "infrastructure-demo\tWarning\tBackOff\tPod\tpayment-abc\t"
        "pod-uid-123\t7\t2026-09-02T09:58:00Z\t"
        "2026-09-02T10:01:00Z\tBack-off restarting failed container\n";

    const KubernetesMetadataSnapshot metadata =
        parse_kubernetes_metadata(pod_output, replica_set_output,
                                  deployment_output, event_output);

    bool passed = true;
    passed &= expect(metadata.available, "marks parsed metadata available");
    passed &= expect(metadata.containers_by_id.size() == 2,
                     "parses each running container status");

    const auto payment_iterator = metadata.containers_by_id.find(payment_id);
    passed &= expect(payment_iterator != metadata.containers_by_id.end(),
                     "removes the container runtime prefix");
    if (payment_iterator != metadata.containers_by_id.end()) {
        const KubernetesContainerIdentity& identity = payment_iterator->second;
        passed &= expect(identity.namespace_name == "infrastructure-demo",
                         "reads namespace");
        passed &= expect(identity.pod_name == "payment-abc", "reads pod name");
        passed &= expect(identity.pod_uid == "pod-uid-123", "reads pod UID");
        passed &= expect(identity.node_name == "ubuntu-vm", "reads node name");
        passed &= expect(identity.container_name == "payment",
                         "reads container name");
        passed &= expect(identity.ready, "reads container readiness");
        passed &= expect(identity.restart_count == 2, "reads restart count");
        passed &= expect(identity.pod_qos_class == "Burstable" &&
                             identity.pod_ready && identity.pod_scheduled &&
                             identity.pod_initialized,
                         "reads pod QoS and conditions");
        passed &= expect(identity.state == "Running" &&
                             identity.last_termination_reason == "OOMKilled" &&
                             identity.last_exit_code == 137,
                         "reads current and previous container lifecycle state");
        passed &= expect(identity.started_at == "2026-09-02T10:00:00Z" &&
                             identity.last_finished_at ==
                                 "2026-09-02T09:59:00Z",
                         "reads container lifecycle timestamps");
        passed &= expect(identity.cpu_request_available &&
                             std::abs(identity.cpu_request_cores - 0.25) <
                                 0.0001 &&
                             identity.cpu_limit_available &&
                             std::abs(identity.cpu_limit_cores - 1.0) < 0.0001,
                         "normalizes Kubernetes CPU requests and limits");
        passed &= expect(identity.memory_request_available &&
                             identity.memory_request_bytes == 134217728 &&
                             identity.memory_limit_available &&
                             identity.memory_limit_bytes == 536870912,
                         "normalizes Kubernetes memory requests and limits");
        passed &= expect(identity.workload_kind == "Deployment" &&
                             identity.workload_name == "payment" &&
                             identity.workload_uid == "deployment-uid-123",
                         "resolves ReplicaSet owner to Deployment");
    }

    const auto deployment_iterator = metadata.deployments_by_key.find(
        "infrastructure-demo\npayment");
    passed &= expect(deployment_iterator != metadata.deployments_by_key.end(),
                     "parses deployment status");
    if (deployment_iterator != metadata.deployments_by_key.end()) {
        const KubernetesDeploymentInfo& deployment =
            deployment_iterator->second;
        passed &= expect(deployment.desired_replicas == 3 &&
                             deployment.ready_replicas == 2 &&
                             deployment.unavailable_replicas == 1,
                         "reads deployment desired and actual replicas");
        passed &= expect(deployment.generation == 4 &&
                             deployment.observed_generation == 4,
                         "reads deployment rollout generations");
    }
    passed &= expect(metadata.events.size() == 1 &&
                         metadata.events[0].reason == "BackOff" &&
                         metadata.events[0].count == 7 &&
                         metadata.events[0].message ==
                             "Back-off restarting failed container",
                     "parses Kubernetes warning events");

    ContainerMetric payment_metric;
    payment_metric.container_id = payment_id;
    ContainerMetric unmatched_metric;
    unmatched_metric.container_id = unmatched_id;
    std::vector<ContainerMetric> metrics = {payment_metric, unmatched_metric};
    attach_kubernetes_identity(metadata, metrics);

    passed &= expect(metrics[0].kubernetes_identity_available,
                     "enriches an exact full container ID match");
    passed &= expect(metrics[0].pod_name == "payment-abc" &&
                         metrics[0].container_name == "payment",
                     "copies Kubernetes identity into the metric");
    passed &= expect(metrics[0].kubernetes_cpu_limit_available &&
                         metrics[0].kubernetes_memory_limit_bytes == 536870912 &&
                         metrics[0].last_termination_reason == "OOMKilled",
                     "copies Kubernetes resources and lifecycle into the metric");
    passed &= expect(!metrics[1].kubernetes_identity_available,
                     "preserves an unmatched Linux container metric");

    return passed ? 0 : 1;
}
