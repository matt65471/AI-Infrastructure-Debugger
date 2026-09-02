#include "kubernetes_metadata_collector.h"

#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr const char* kPodCommand =
    R"(/usr/local/bin/k3s kubectl get pods --all-namespaces --request-timeout=2s -o jsonpath='{range .items[*]}{.metadata.namespace}{"\t"}{.metadata.name}{"\t"}{.metadata.uid}{"\t"}{.spec.nodeName}{"\t"}{.status.phase}{"\t"}{.metadata.ownerReferences[0].kind}{"\t"}{.metadata.ownerReferences[0].name}{"\t"}{range .status.containerStatuses[*]}{.containerID}{"|"}{.name}{"|"}{.image}{"|"}{.restartCount}{"|"}{.ready}{";"}{end}{range .status.initContainerStatuses[*]}{.containerID}{"|"}{.name}{"|"}{.image}{"|"}{.restartCount}{"|"}{.ready}{";"}{end}{range .status.ephemeralContainerStatuses[*]}{.containerID}{"|"}{.name}{"|"}{.image}{"|"}{.restartCount}{"|"}{.ready}{";"}{end}{"\n"}{end}' 2>/dev/null)";

constexpr const char* kReplicaSetCommand =
    R"(/usr/local/bin/k3s kubectl get replicasets --all-namespaces --request-timeout=2s -o jsonpath='{range .items[*]}{.metadata.namespace}{"\t"}{.metadata.name}{"\t"}{.metadata.ownerReferences[0].kind}{"\t"}{.metadata.ownerReferences[0].name}{"\n"}{end}' 2>/dev/null)";

struct CommandResult {
    bool succeeded = false;
    std::string output;
};

struct WorkloadOwner {
    std::string kind;
    std::string name;
};

CommandResult run_command(const char* command) {
    CommandResult result;
    FILE* pipe = popen(command, "r");
    if (pipe == nullptr) {
        return result;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }
    result.succeeded = pclose(pipe) == 0;
    return result;
}

std::vector<std::string> split_preserving_empty(const std::string& value,
                                                char delimiter) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t separator = value.find(delimiter, start);
        if (separator == std::string::npos) {
            fields.push_back(value.substr(start));
            return fields;
        }
        fields.push_back(value.substr(start, separator - start));
        start = separator + 1;
    }
}

std::string strip_runtime_prefix(const std::string& container_id) {
    const std::size_t separator = container_id.find("://");
    return separator == std::string::npos
               ? container_id
               : container_id.substr(separator + 3);
}

std::uint64_t parse_unsigned(const std::string& value) {
    try {
        return std::stoull(value);
    } catch (const std::exception&) {
        return 0;
    }
}

std::string namespaced_key(const std::string& namespace_name,
                           const std::string& name) {
    return namespace_name + "\n" + name;
}

std::unordered_map<std::string, WorkloadOwner> parse_replica_sets(
    const std::string& output) {
    std::unordered_map<std::string, WorkloadOwner> replica_sets;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const std::vector<std::string> fields =
            split_preserving_empty(line, '\t');
        if (fields.size() < 4 || fields[0].empty() || fields[1].empty()) {
            continue;
        }
        replica_sets[namespaced_key(fields[0], fields[1])] =
            WorkloadOwner{fields[2], fields[3]};
    }
    return replica_sets;
}

WorkloadOwner resolve_workload(
    const std::string& namespace_name,
    const std::string& owner_kind,
    const std::string& owner_name,
    const std::unordered_map<std::string, WorkloadOwner>& replica_sets) {
    if (owner_kind != "ReplicaSet") {
        return WorkloadOwner{owner_kind, owner_name};
    }

    const auto iterator =
        replica_sets.find(namespaced_key(namespace_name, owner_name));
    if (iterator == replica_sets.end() || iterator->second.name.empty()) {
        return WorkloadOwner{owner_kind, owner_name};
    }
    return iterator->second;
}

}  // namespace

KubernetesMetadataSnapshot parse_kubernetes_metadata(
    const std::string& pod_output,
    const std::string& replica_set_output) {
    KubernetesMetadataSnapshot snapshot;
    snapshot.available = true;
    const std::unordered_map<std::string, WorkloadOwner> replica_sets =
        parse_replica_sets(replica_set_output);

    std::istringstream lines(pod_output);
    std::string line;
    while (std::getline(lines, line)) {
        const std::vector<std::string> fields =
            split_preserving_empty(line, '\t');
        if (fields.size() < 8) {
            continue;
        }

        const WorkloadOwner workload = resolve_workload(
            fields[0], fields[5], fields[6], replica_sets);
        for (const std::string& encoded_container :
             split_preserving_empty(fields[7], ';')) {
            if (encoded_container.empty()) {
                continue;
            }
            const std::vector<std::string> container_fields =
                split_preserving_empty(encoded_container, '|');
            if (container_fields.size() < 5) {
                continue;
            }

            const std::string container_id =
                strip_runtime_prefix(container_fields[0]);
            if (container_id.empty()) {
                continue;
            }

            KubernetesContainerIdentity identity;
            identity.container_id = container_id;
            identity.container_name = container_fields[1];
            identity.image = container_fields[2];
            identity.namespace_name = fields[0];
            identity.pod_name = fields[1];
            identity.pod_uid = fields[2];
            identity.node_name = fields[3];
            identity.pod_phase = fields[4];
            identity.ready = container_fields[4] == "true";
            identity.restart_count = parse_unsigned(container_fields[3]);
            identity.workload_kind = workload.kind;
            identity.workload_name = workload.name;
            snapshot.containers_by_id[container_id] = std::move(identity);
        }
    }

    return snapshot;
}

void attach_kubernetes_identity(
    const KubernetesMetadataSnapshot& metadata,
    std::vector<ContainerMetric>& metrics) {
    for (ContainerMetric& metric : metrics) {
        const auto iterator = metadata.containers_by_id.find(metric.container_id);
        if (iterator == metadata.containers_by_id.end()) {
            continue;
        }

        const KubernetesContainerIdentity& identity = iterator->second;
        metric.kubernetes_identity_available = true;
        metric.namespace_name = identity.namespace_name;
        metric.pod_name = identity.pod_name;
        metric.pod_uid = identity.pod_uid;
        metric.node_name = identity.node_name;
        metric.container_name = identity.container_name;
        metric.image = identity.image;
        metric.pod_phase = identity.pod_phase;
        metric.container_ready = identity.ready;
        metric.restart_count = identity.restart_count;
        metric.workload_kind = identity.workload_kind;
        metric.workload_name = identity.workload_name;
    }
}

KubernetesMetadataSnapshot KubernetesMetadataCollector::read_snapshot() const {
    const CommandResult pods = run_command(kPodCommand);
    if (!pods.succeeded) {
        return {};
    }

    const CommandResult replica_sets = run_command(kReplicaSetCommand);
    return parse_kubernetes_metadata(
        pods.output,
        replica_sets.succeeded ? replica_sets.output : std::string{});
}
