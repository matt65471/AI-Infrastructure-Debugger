#ifndef KUBERNETES_METADATA_COLLECTOR_H
#define KUBERNETES_METADATA_COLLECTOR_H

#include "container_metric_calculator.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct KubernetesContainerIdentity {
    std::string container_id;
    std::string container_name;
    std::string image;
    std::string namespace_name;
    std::string pod_name;
    std::string pod_uid;
    std::string node_name;
    std::string pod_phase;
    bool ready = false;
    std::uint64_t restart_count = 0;
    std::string workload_kind;
    std::string workload_name;
};

struct KubernetesMetadataSnapshot {
    bool available = false;
    std::unordered_map<std::string, KubernetesContainerIdentity>
        containers_by_id;
};

KubernetesMetadataSnapshot parse_kubernetes_metadata(
    const std::string& pod_output,
    const std::string& replica_set_output);

void attach_kubernetes_identity(
    const KubernetesMetadataSnapshot& metadata,
    std::vector<ContainerMetric>& metrics);

class KubernetesMetadataCollector {
public:
    KubernetesMetadataSnapshot read_snapshot() const;
};

#endif
