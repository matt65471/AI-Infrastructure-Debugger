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
    std::string pod_qos_class;
    bool pod_ready = false;
    bool pod_scheduled = false;
    bool pod_initialized = false;
    bool ready = false;
    std::uint64_t restart_count = 0;
    std::string state;
    std::string state_reason;
    std::int64_t exit_code = 0;
    std::string last_termination_reason;
    std::int64_t last_exit_code = 0;
    std::string started_at;
    std::string finished_at;
    std::string last_finished_at;
    bool cpu_request_available = false;
    double cpu_request_cores = 0.0;
    bool cpu_limit_available = false;
    double cpu_limit_cores = 0.0;
    bool memory_request_available = false;
    std::uint64_t memory_request_bytes = 0;
    bool memory_limit_available = false;
    std::uint64_t memory_limit_bytes = 0;
    std::string workload_kind;
    std::string workload_name;
    std::string workload_uid;
};

struct KubernetesDeploymentInfo {
    std::string namespace_name;
    std::string deployment_name;
    std::string deployment_uid;
    std::uint64_t generation = 0;
    std::uint64_t observed_generation = 0;
    std::uint64_t desired_replicas = 0;
    std::uint64_t ready_replicas = 0;
    std::uint64_t available_replicas = 0;
    std::uint64_t unavailable_replicas = 0;
    std::uint64_t updated_replicas = 0;
};

struct KubernetesEvent {
    std::string namespace_name;
    std::string event_type;
    std::string reason;
    std::string object_kind;
    std::string object_name;
    std::string object_uid;
    std::uint64_t count = 0;
    std::string first_timestamp;
    std::string last_timestamp;
    std::string message;
};

struct KubernetesMetadataSnapshot {
    bool available = false;
    std::unordered_map<std::string, KubernetesContainerIdentity>
        containers_by_id;
    std::unordered_map<std::string, KubernetesDeploymentInfo>
        deployments_by_key;
    std::vector<KubernetesEvent> events;
};

KubernetesMetadataSnapshot parse_kubernetes_metadata(
    const std::string& pod_output,
    const std::string& replica_set_output,
    const std::string& deployment_output = "",
    const std::string& event_output = "");

void attach_kubernetes_identity(
    const KubernetesMetadataSnapshot& metadata,
    std::vector<ContainerMetric>& metrics);

class KubernetesMetadataCollector {
public:
    KubernetesMetadataSnapshot read_snapshot() const;
};

#endif
