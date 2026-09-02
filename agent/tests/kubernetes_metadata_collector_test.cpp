#include "kubernetes_metadata_collector.h"

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
        "|payment|infrastructure-debugger/payment:v1|2|true;containerd://" +
        sidecar_id + "|logger|example/logger:v1|0|false;\n"
        "infrastructure-demo\tpending-pod\tpod-uid-456\tubuntu-vm\tPending\t"
        "ReplicaSet\tpending-abc\t\n";
    const std::string replica_set_output =
        "infrastructure-demo\tpayment-abc\tDeployment\tpayment\n";

    const KubernetesMetadataSnapshot metadata =
        parse_kubernetes_metadata(pod_output, replica_set_output);

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
        passed &= expect(identity.workload_kind == "Deployment" &&
                             identity.workload_name == "payment",
                         "resolves ReplicaSet owner to Deployment");
    }

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
    passed &= expect(!metrics[1].kubernetes_identity_available,
                     "preserves an unmatched Linux container metric");

    return passed ? 0 : 1;
}
