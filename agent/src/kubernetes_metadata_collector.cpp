#include "kubernetes_metadata_collector.h"

#include <cstdio>
#include <exception>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr const char* kPodCommand =
    R"(/usr/local/bin/k3s kubectl get pods --all-namespaces --request-timeout=2s -o jsonpath='{range .items[*]}{.metadata.namespace}{"\t"}{.metadata.name}{"\t"}{.metadata.uid}{"\t"}{.spec.nodeName}{"\t"}{.status.phase}{"\t"}{.metadata.ownerReferences[0].kind}{"\t"}{.metadata.ownerReferences[0].name}{"\t"}{range .status.containerStatuses[*]}{.containerID}{"|"}{.name}{"|"}{.image}{"|"}{.restartCount}{"|"}{.ready}{"|"}{.state.waiting.reason}{"|"}{.state.terminated.reason}{"|"}{.state.terminated.exitCode}{"|"}{.lastState.terminated.reason}{"|"}{.lastState.terminated.exitCode}{"|"}{.state.running.startedAt}{"|"}{.state.terminated.finishedAt}{"|"}{.lastState.terminated.finishedAt}{";"}{end}{"\t"}{.status.qosClass}{"\t"}{.status.conditions[?(@.type=="Ready")].status}{"\t"}{.status.conditions[?(@.type=="PodScheduled")].status}{"\t"}{.status.conditions[?(@.type=="Initialized")].status}{"\t"}{range .spec.containers[*]}{.name}{"|"}{.resources.requests.cpu}{"|"}{.resources.limits.cpu}{"|"}{.resources.requests.memory}{"|"}{.resources.limits.memory}{";"}{end}{"\n"}{end}' 2>/dev/null)";

constexpr const char* kReplicaSetCommand =
    R"(/usr/local/bin/k3s kubectl get replicasets --all-namespaces --request-timeout=2s -o jsonpath='{range .items[*]}{.metadata.namespace}{"\t"}{.metadata.name}{"\t"}{.metadata.ownerReferences[0].kind}{"\t"}{.metadata.ownerReferences[0].name}{"\t"}{.metadata.ownerReferences[0].uid}{"\n"}{end}' 2>/dev/null)";

constexpr const char* kDeploymentCommand =
    R"(/usr/local/bin/k3s kubectl get deployments --all-namespaces --request-timeout=2s -o jsonpath='{range .items[*]}{.metadata.namespace}{"\t"}{.metadata.name}{"\t"}{.metadata.uid}{"\t"}{.metadata.generation}{"\t"}{.status.observedGeneration}{"\t"}{.spec.replicas}{"\t"}{.status.readyReplicas}{"\t"}{.status.availableReplicas}{"\t"}{.status.unavailableReplicas}{"\t"}{.status.updatedReplicas}{"\n"}{end}' 2>/dev/null)";

constexpr const char* kEventCommand =
    R"(/usr/local/bin/k3s kubectl get events --all-namespaces --request-timeout=2s --sort-by=.metadata.creationTimestamp -o jsonpath='{range .items[*]}{.metadata.namespace}{"\t"}{.type}{"\t"}{.reason}{"\t"}{.involvedObject.kind}{"\t"}{.involvedObject.name}{"\t"}{.involvedObject.uid}{"\t"}{.count}{"\t"}{.firstTimestamp}{"\t"}{.lastTimestamp}{"\t"}{.message}{"\n"}{end}' 2>/dev/null)";

struct CommandResult {
    bool succeeded = false;
    std::string output;
};

struct WorkloadOwner {
    std::string kind;
    std::string name;
    std::string uid;
};

struct ContainerResources {
    bool cpu_request_available = false;
    double cpu_request_cores = 0.0;
    bool cpu_limit_available = false;
    double cpu_limit_cores = 0.0;
    bool memory_request_available = false;
    std::uint64_t memory_request_bytes = 0;
    bool memory_limit_available = false;
    std::uint64_t memory_limit_bytes = 0;
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

std::int64_t parse_signed(const std::string& value) {
    try {
        return std::stoll(value);
    } catch (const std::exception&) {
        return 0;
    }
}

bool parse_cpu_cores(const std::string& value, double& cores) {
    if (value.empty()) {
        return false;
    }
    try {
        if (value.back() == 'm') {
            cores = std::stod(value.substr(0, value.size() - 1)) / 1'000.0;
        } else if (value.back() == 'u') {
            cores = std::stod(value.substr(0, value.size() - 1)) / 1'000'000.0;
        } else if (value.back() == 'n') {
            cores =
                std::stod(value.substr(0, value.size() - 1)) / 1'000'000'000.0;
        } else {
            cores = std::stod(value);
        }
        return true;
    } catch (const std::exception&) {
        cores = 0.0;
        return false;
    }
}

bool parse_memory_bytes(const std::string& value, std::uint64_t& bytes) {
    if (value.empty()) {
        return false;
    }

    struct Suffix {
        const char* text;
        long double multiplier;
    };
    static const Suffix suffixes[] = {
        {"Ei", 1.152921504606846976e18L},
        {"Pi", 1.125899906842624e15L},
        {"Ti", 1.099511627776e12L},
        {"Gi", 1.073741824e9L},
        {"Mi", 1.048576e6L},
        {"Ki", 1024.0L},
        {"E", 1.0e18L},
        {"P", 1.0e15L},
        {"T", 1.0e12L},
        {"G", 1.0e9L},
        {"M", 1.0e6L},
        {"K", 1.0e3L},
        {"k", 1.0e3L},
    };

    std::string number = value;
    long double multiplier = 1.0L;
    for (const Suffix& suffix : suffixes) {
        const std::string suffix_text = suffix.text;
        if (value.size() >= suffix_text.size() &&
            value.compare(value.size() - suffix_text.size(),
                          suffix_text.size(),
                          suffix_text) == 0) {
            number = value.substr(0, value.size() - suffix_text.size());
            multiplier = suffix.multiplier;
            break;
        }
    }

    try {
        const long double result = std::stold(number) * multiplier;
        if (result < 0.0L ||
            result > static_cast<long double>(
                         std::numeric_limits<std::uint64_t>::max())) {
            return false;
        }
        bytes = static_cast<std::uint64_t>(result);
        return true;
    } catch (const std::exception&) {
        bytes = 0;
        return false;
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
            WorkloadOwner{fields[2], fields[3],
                          fields.size() > 4 ? fields[4] : std::string{}};
    }
    return replica_sets;
}

WorkloadOwner resolve_workload(
    const std::string& namespace_name,
    const std::string& owner_kind,
    const std::string& owner_name,
    const std::unordered_map<std::string, WorkloadOwner>& replica_sets) {
    if (owner_kind != "ReplicaSet") {
        return WorkloadOwner{owner_kind, owner_name, {}};
    }

    const auto iterator =
        replica_sets.find(namespaced_key(namespace_name, owner_name));
    if (iterator == replica_sets.end() || iterator->second.name.empty()) {
        return WorkloadOwner{owner_kind, owner_name, {}};
    }
    return iterator->second;
}

std::unordered_map<std::string, ContainerResources> parse_container_resources(
    const std::string& encoded_resources) {
    std::unordered_map<std::string, ContainerResources> resources_by_name;
    for (const std::string& encoded :
         split_preserving_empty(encoded_resources, ';')) {
        if (encoded.empty()) {
            continue;
        }
        const std::vector<std::string> fields =
            split_preserving_empty(encoded, '|');
        if (fields.size() < 5 || fields[0].empty()) {
            continue;
        }
        ContainerResources resources;
        resources.cpu_request_available =
            parse_cpu_cores(fields[1], resources.cpu_request_cores);
        resources.cpu_limit_available =
            parse_cpu_cores(fields[2], resources.cpu_limit_cores);
        resources.memory_request_available =
            parse_memory_bytes(fields[3], resources.memory_request_bytes);
        resources.memory_limit_available =
            parse_memory_bytes(fields[4], resources.memory_limit_bytes);
        resources_by_name[fields[0]] = resources;
    }
    return resources_by_name;
}

void parse_deployments(const std::string& output,
                       KubernetesMetadataSnapshot& snapshot) {
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const std::vector<std::string> fields =
            split_preserving_empty(line, '\t');
        if (fields.size() < 10 || fields[0].empty() || fields[1].empty()) {
            continue;
        }
        KubernetesDeploymentInfo deployment;
        deployment.namespace_name = fields[0];
        deployment.deployment_name = fields[1];
        deployment.deployment_uid = fields[2];
        deployment.generation = parse_unsigned(fields[3]);
        deployment.observed_generation = parse_unsigned(fields[4]);
        deployment.desired_replicas = parse_unsigned(fields[5]);
        deployment.ready_replicas = parse_unsigned(fields[6]);
        deployment.available_replicas = parse_unsigned(fields[7]);
        deployment.unavailable_replicas = parse_unsigned(fields[8]);
        deployment.updated_replicas = parse_unsigned(fields[9]);
        snapshot.deployments_by_key[
            namespaced_key(deployment.namespace_name,
                           deployment.deployment_name)] = deployment;
    }
}

void parse_events(const std::string& output,
                  KubernetesMetadataSnapshot& snapshot) {
    constexpr std::size_t kMaximumEvents = 50;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const std::vector<std::string> fields =
            split_preserving_empty(line, '\t');
        if (fields.size() < 9 || fields[2].empty()) {
            continue;
        }
        KubernetesEvent event;
        event.namespace_name = fields[0];
        event.event_type = fields[1];
        event.reason = fields[2];
        event.object_kind = fields[3];
        event.object_name = fields[4];
        event.object_uid = fields[5];
        event.count = parse_unsigned(fields[6]);
        event.first_timestamp = fields[7];
        event.last_timestamp = fields[8];
        event.message = fields.size() > 9 ? fields[9] : std::string{};
        snapshot.events.push_back(std::move(event));
        if (snapshot.events.size() > kMaximumEvents) {
            snapshot.events.erase(snapshot.events.begin());
        }
    }
}

}  // namespace

KubernetesMetadataSnapshot parse_kubernetes_metadata(
    const std::string& pod_output,
    const std::string& replica_set_output,
    const std::string& deployment_output,
    const std::string& event_output) {
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
        const std::unordered_map<std::string, ContainerResources>
            resources_by_name = parse_container_resources(
                fields.size() > 12 ? fields[12] : std::string{});
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
            identity.pod_qos_class =
                fields.size() > 8 ? fields[8] : std::string{};
            identity.pod_ready = fields.size() > 9 && fields[9] == "True";
            identity.pod_scheduled =
                fields.size() > 10 && fields[10] == "True";
            identity.pod_initialized =
                fields.size() > 11 && fields[11] == "True";
            identity.ready = container_fields[4] == "true";
            identity.restart_count = parse_unsigned(container_fields[3]);
            const std::string waiting_reason =
                container_fields.size() > 5 ? container_fields[5]
                                            : std::string{};
            const std::string terminated_reason =
                container_fields.size() > 6 ? container_fields[6]
                                            : std::string{};
            identity.exit_code = container_fields.size() > 7
                                     ? parse_signed(container_fields[7])
                                     : 0;
            identity.last_termination_reason =
                container_fields.size() > 8 ? container_fields[8]
                                            : std::string{};
            identity.last_exit_code = container_fields.size() > 9
                                          ? parse_signed(container_fields[9])
                                          : 0;
            identity.started_at = container_fields.size() > 10
                                      ? container_fields[10]
                                      : std::string{};
            identity.finished_at = container_fields.size() > 11
                                       ? container_fields[11]
                                       : std::string{};
            identity.last_finished_at = container_fields.size() > 12
                                            ? container_fields[12]
                                            : std::string{};
            if (!waiting_reason.empty()) {
                identity.state = "Waiting";
                identity.state_reason = waiting_reason;
            } else if (!terminated_reason.empty() ||
                       !identity.finished_at.empty()) {
                identity.state = "Terminated";
                identity.state_reason = terminated_reason;
            } else {
                identity.state = "Running";
            }

            const auto resources_iterator =
                resources_by_name.find(identity.container_name);
            if (resources_iterator != resources_by_name.end()) {
                const ContainerResources& resources =
                    resources_iterator->second;
                identity.cpu_request_available =
                    resources.cpu_request_available;
                identity.cpu_request_cores = resources.cpu_request_cores;
                identity.cpu_limit_available = resources.cpu_limit_available;
                identity.cpu_limit_cores = resources.cpu_limit_cores;
                identity.memory_request_available =
                    resources.memory_request_available;
                identity.memory_request_bytes =
                    resources.memory_request_bytes;
                identity.memory_limit_available =
                    resources.memory_limit_available;
                identity.memory_limit_bytes = resources.memory_limit_bytes;
            }
            identity.workload_kind = workload.kind;
            identity.workload_name = workload.name;
            identity.workload_uid = workload.uid;
            snapshot.containers_by_id[container_id] = std::move(identity);
        }
    }

    parse_deployments(deployment_output, snapshot);
    parse_events(event_output, snapshot);
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
        metric.pod_qos_class = identity.pod_qos_class;
        metric.pod_ready = identity.pod_ready;
        metric.pod_scheduled = identity.pod_scheduled;
        metric.pod_initialized = identity.pod_initialized;
        metric.container_ready = identity.ready;
        metric.restart_count = identity.restart_count;
        metric.container_state = identity.state;
        metric.state_reason = identity.state_reason;
        metric.exit_code = identity.exit_code;
        metric.last_termination_reason = identity.last_termination_reason;
        metric.last_exit_code = identity.last_exit_code;
        metric.started_at = identity.started_at;
        metric.finished_at = identity.finished_at;
        metric.last_finished_at = identity.last_finished_at;
        metric.cpu_request_available = identity.cpu_request_available;
        metric.cpu_request_cores = identity.cpu_request_cores;
        metric.kubernetes_cpu_limit_available = identity.cpu_limit_available;
        metric.kubernetes_cpu_limit_cores = identity.cpu_limit_cores;
        metric.memory_request_available = identity.memory_request_available;
        metric.memory_request_bytes = identity.memory_request_bytes;
        metric.kubernetes_memory_limit_available =
            identity.memory_limit_available;
        metric.kubernetes_memory_limit_bytes = identity.memory_limit_bytes;
        metric.workload_kind = identity.workload_kind;
        metric.workload_name = identity.workload_name;
        metric.workload_uid = identity.workload_uid;
    }
}

KubernetesMetadataSnapshot KubernetesMetadataCollector::read_snapshot() const {
    const CommandResult pods = run_command(kPodCommand);
    if (!pods.succeeded) {
        return {};
    }

    const CommandResult replica_sets = run_command(kReplicaSetCommand);
    const CommandResult deployments = run_command(kDeploymentCommand);
    const CommandResult events = run_command(kEventCommand);
    return parse_kubernetes_metadata(
        pods.output,
        replica_sets.succeeded ? replica_sets.output : std::string{},
        deployments.succeeded ? deployments.output : std::string{},
        events.succeeded ? events.output : std::string{});
}
