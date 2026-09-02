#include "json_formatter.h"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main() {
    TelemetrySnapshot snapshot;
    snapshot.node.timestamp_unix_ms = 1234;
    snapshot.node.hostname = "node-\"one\"";
    snapshot.node.cpu_usage_percent = 25.5;
    snapshot.node.pressure.cpu.available = true;
    snapshot.node.pressure.cpu.some.avg10 = 1.25;
    snapshot.cgroups.cgroup_v2_available = true;
    snapshot.kubernetes_metadata_available = true;

    ProcessMetric process;
    process.pid = 42;
    process.name = "worker\nprocess";
    process.state = 'R';
    snapshot.processes.push_back(process);

    ContainerMetric container;
    container.container_id = std::string(64, 'a');
    container.process_ids = {42};
    snapshot.containers.push_back(container);

    KubernetesEvent event;
    event.reason = "BackOff";
    event.message = "retry \"later\"";
    snapshot.kubernetes_events.push_back(event);

    const std::string json = format_snapshot_as_json(snapshot);
    bool passed = true;
    passed &= expect(json.front() == '{' && json.back() == '}',
                     "writes one JSON object");
    passed &= expect(json.find("\"hostname\":\"node-\\\"one\\\"\"") !=
                         std::string::npos,
                     "escapes quotes in strings");
    passed &= expect(json.find("worker\\nprocess") != std::string::npos,
                     "escapes newlines in strings");
    passed &= expect(json.find("\"processes\":[{\"pid\":42") !=
                         std::string::npos,
                     "serializes all process metrics");
    passed &= expect(json.find("\"cgroup_v2_available\":true") !=
                         std::string::npos,
                     "serializes collector availability");
    passed &= expect(json.find("\"reason\":\"BackOff\"") !=
                         std::string::npos,
                     "serializes Kubernetes events");
    return passed ? 0 : 1;
}
