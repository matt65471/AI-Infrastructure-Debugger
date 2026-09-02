#include "json_formatter.h"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string quote(const std::string& value) {
    std::ostringstream stream;
    stream << '"';
    for (const unsigned char character : value) {
        switch (character) {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (character < 0x20) {
                    stream << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0')
                           << static_cast<unsigned int>(character)
                           << std::dec << std::setfill(' ');
                } else {
                    stream << character;
                }
        }
    }
    stream << '"';
    return stream.str();
}

class JsonObject {
public:
    explicit JsonObject(std::ostringstream& stream) : stream_(stream) {
        stream_ << '{';
    }

    ~JsonObject() { stream_ << '}'; }

    void key(const std::string& name) {
        if (!first_) {
            stream_ << ',';
        }
        first_ = false;
        stream_ << quote(name) << ':';
    }

private:
    std::ostringstream& stream_;
    bool first_ = true;
};

void write_bool(std::ostringstream& stream, bool value) {
    stream << (value ? "true" : "false");
}

void write_double(std::ostringstream& stream, double value) {
    if (std::isfinite(value)) {
        stream << std::setprecision(15) << value;
    } else {
        stream << "null";
    }
}

template <typename Value>
void field(JsonObject& object,
           std::ostringstream& stream,
           const std::string& name,
           const Value& value) {
    object.key(name);
    stream << value;
}

void string_field(JsonObject& object,
                  std::ostringstream& stream,
                  const std::string& name,
                  const std::string& value) {
    object.key(name);
    stream << quote(value);
}

void bool_field(JsonObject& object,
                std::ostringstream& stream,
                const std::string& name,
                bool value) {
    object.key(name);
    write_bool(stream, value);
}

void double_field(JsonObject& object,
                  std::ostringstream& stream,
                  const std::string& name,
                  double value) {
    object.key(name);
    write_double(stream, value);
}

void write_pressure_values(std::ostringstream& stream,
                           const PressureValues& values) {
    JsonObject object(stream);
    double_field(object, stream, "avg10", values.avg10);
    double_field(object, stream, "avg60", values.avg60);
    double_field(object, stream, "avg300", values.avg300);
    field(object, stream, "total_usec", values.total_usec);
}

void write_pressure(std::ostringstream& stream,
                    const ResourcePressure& pressure) {
    JsonObject object(stream);
    bool_field(object, stream, "available", pressure.available);
    object.key("some");
    write_pressure_values(stream, pressure.some);
    bool_field(object, stream, "full_available", pressure.full_available);
    object.key("full");
    write_pressure_values(stream, pressure.full);
}

void write_node(std::ostringstream& stream, const NodeMetric& node) {
    JsonObject object(stream);
    field(object, stream, "timestamp_unix_ms", node.timestamp_unix_ms);
    string_field(object, stream, "hostname", node.hostname);
    field(object, stream, "logical_cpu_count", node.logical_cpu_count);
    double_field(object, stream, "cpu_usage_percent", node.cpu_usage_percent);
    double_field(object, stream, "memory_usage_percent",
                 node.memory_usage_percent);
    field(object, stream, "memory_total_kb", node.memory_total_kb);
    field(object, stream, "memory_available_kb", node.memory_available_kb);
    field(object, stream, "swap_total_kb", node.swap_total_kb);
    field(object, stream, "swap_free_kb", node.swap_free_kb);
    field(object, stream, "network_rx_bytes_per_second",
          node.network_rx_bytes_per_second);
    field(object, stream, "network_tx_bytes_per_second",
          node.network_tx_bytes_per_second);
    field(object, stream, "tcp_retransmits_per_second",
          node.tcp_retransmits_per_second);
    field(object, stream, "tcp_in_segments_per_second",
          node.tcp_in_segments_per_second);
    field(object, stream, "tcp_out_segments_per_second",
          node.tcp_out_segments_per_second);
    field(object, stream, "tcp_reset_count_delta", node.tcp_reset_count_delta);
    field(object, stream, "tcp_listen_overflows_delta",
          node.tcp_listen_overflows_delta);
    field(object, stream, "tcp_listen_drops_delta",
          node.tcp_listen_drops_delta);
    field(object, stream, "tcp_timeouts_delta", node.tcp_timeouts_delta);
    field(object, stream, "disk_read_bytes_per_second",
          node.disk_read_bytes_per_second);
    field(object, stream, "disk_write_bytes_per_second",
          node.disk_write_bytes_per_second);
    field(object, stream, "disk_reads_per_second", node.disk_reads_per_second);
    field(object, stream, "disk_writes_per_second",
          node.disk_writes_per_second);
    field(object, stream, "disk_io_time_ms_delta", node.disk_io_time_ms_delta);
    object.key("pressure");
    {
        JsonObject pressure(stream);
        bool_field(pressure, stream, "load_average_available",
                   node.pressure.load_average_available);
        double_field(pressure, stream, "load_average_1m",
                     node.pressure.load_average_1m);
        double_field(pressure, stream, "load_average_5m",
                     node.pressure.load_average_5m);
        double_field(pressure, stream, "load_average_15m",
                     node.pressure.load_average_15m);
        field(pressure, stream, "runnable_processes",
              node.pressure.runnable_processes);
        field(pressure, stream, "total_processes",
              node.pressure.total_processes);
        pressure.key("cpu");
        write_pressure(stream, node.pressure.cpu);
        pressure.key("memory");
        write_pressure(stream, node.pressure.memory);
        pressure.key("io");
        write_pressure(stream, node.pressure.io);
    }
}

void write_int_array(std::ostringstream& stream,
                     const std::vector<int>& values) {
    stream << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) stream << ',';
        stream << values[index];
    }
    stream << ']';
}

void write_string_array(std::ostringstream& stream,
                        const std::vector<std::string>& values) {
    stream << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) stream << ',';
        stream << quote(values[index]);
    }
    stream << ']';
}

void write_process(std::ostringstream& stream, const ProcessMetric& process) {
    JsonObject object(stream);
    field(object, stream, "pid", process.pid);
    string_field(object, stream, "name", process.name);
    string_field(object, stream, "state", std::string(1, process.state));
    double_field(object, stream, "cpu_usage_percent",
                 process.cpu_usage_percent);
    field(object, stream, "resident_memory_kb", process.resident_memory_kb);
    field(object, stream, "virtual_memory_kb", process.virtual_memory_kb);
    field(object, stream, "thread_count", process.thread_count);
    field(object, stream, "read_bytes_per_second",
          process.read_bytes_per_second);
    field(object, stream, "write_bytes_per_second",
          process.write_bytes_per_second);
}

void write_container(std::ostringstream& stream,
                     const ContainerMetric& container) {
    JsonObject object(stream);
    field(object, stream, "timestamp_unix_ms", container.timestamp_unix_ms);
    string_field(object, stream, "container_id", container.container_id);
    string_field(object, stream, "cgroup_path", container.cgroup_path);
    bool_field(object, stream, "cpu_usage_available",
               container.cpu_usage_available);
    double_field(object, stream, "cpu_usage_percent",
                 container.cpu_usage_percent);
    field(object, stream, "cpu_usage_usec", container.cpu_usage_usec);
    bool_field(object, stream, "cpu_limit_available",
               container.cpu_limit_available);
    bool_field(object, stream, "cpu_is_unlimited", container.cpu_is_unlimited);
    double_field(object, stream, "cpu_limit_cores", container.cpu_limit_cores);
    field(object, stream, "throttled_periods_delta",
          container.throttled_periods_delta);
    field(object, stream, "throttled_usec_delta",
          container.throttled_usec_delta);
    field(object, stream, "memory_current_bytes",
          container.memory_current_bytes);
    field(object, stream, "memory_max_bytes", container.memory_max_bytes);
    bool_field(object, stream, "memory_is_unlimited",
               container.memory_is_unlimited);
    bool_field(object, stream, "memory_usage_percent_available",
               container.memory_usage_percent_available);
    double_field(object, stream, "memory_usage_percent",
                 container.memory_usage_percent);
    field(object, stream, "memory_high_delta", container.memory_high_delta);
    field(object, stream, "memory_max_delta", container.memory_max_delta);
    field(object, stream, "oom_delta", container.oom_delta);
    field(object, stream, "oom_kill_delta", container.oom_kill_delta);
    object.key("process_ids");
    write_int_array(stream, container.process_ids);
    bool_field(object, stream, "kubernetes_identity_available",
               container.kubernetes_identity_available);
    string_field(object, stream, "namespace", container.namespace_name);
    string_field(object, stream, "pod_name", container.pod_name);
    string_field(object, stream, "pod_uid", container.pod_uid);
    string_field(object, stream, "node_name", container.node_name);
    string_field(object, stream, "container_name", container.container_name);
    string_field(object, stream, "image", container.image);
    string_field(object, stream, "pod_phase", container.pod_phase);
    string_field(object, stream, "pod_qos_class", container.pod_qos_class);
    bool_field(object, stream, "pod_ready", container.pod_ready);
    bool_field(object, stream, "pod_scheduled", container.pod_scheduled);
    bool_field(object, stream, "pod_initialized", container.pod_initialized);
    bool_field(object, stream, "container_ready", container.container_ready);
    field(object, stream, "restart_count", container.restart_count);
    string_field(object, stream, "container_state", container.container_state);
    string_field(object, stream, "state_reason", container.state_reason);
    field(object, stream, "exit_code", container.exit_code);
    string_field(object, stream, "last_termination_reason",
                 container.last_termination_reason);
    field(object, stream, "last_exit_code", container.last_exit_code);
    string_field(object, stream, "started_at", container.started_at);
    string_field(object, stream, "finished_at", container.finished_at);
    string_field(object, stream, "last_finished_at",
                 container.last_finished_at);
    bool_field(object, stream, "cpu_request_available",
               container.cpu_request_available);
    double_field(object, stream, "cpu_request_cores",
                 container.cpu_request_cores);
    bool_field(object, stream, "kubernetes_cpu_limit_available",
               container.kubernetes_cpu_limit_available);
    double_field(object, stream, "kubernetes_cpu_limit_cores",
                 container.kubernetes_cpu_limit_cores);
    bool_field(object, stream, "memory_request_available",
               container.memory_request_available);
    field(object, stream, "memory_request_bytes",
          container.memory_request_bytes);
    bool_field(object, stream, "kubernetes_memory_limit_available",
               container.kubernetes_memory_limit_available);
    field(object, stream, "kubernetes_memory_limit_bytes",
          container.kubernetes_memory_limit_bytes);
    string_field(object, stream, "workload_kind", container.workload_kind);
    string_field(object, stream, "workload_name", container.workload_name);
    string_field(object, stream, "workload_uid", container.workload_uid);
}

void write_pod(std::ostringstream& stream, const PodMetric& pod) {
    JsonObject object(stream);
    field(object, stream, "timestamp_unix_ms", pod.timestamp_unix_ms);
    string_field(object, stream, "node_name", pod.node_name);
    string_field(object, stream, "namespace", pod.namespace_name);
    string_field(object, stream, "pod_name", pod.pod_name);
    string_field(object, stream, "pod_uid", pod.pod_uid);
    string_field(object, stream, "pod_phase", pod.pod_phase);
    string_field(object, stream, "pod_qos_class", pod.pod_qos_class);
    bool_field(object, stream, "pod_ready", pod.pod_ready);
    bool_field(object, stream, "pod_scheduled", pod.pod_scheduled);
    bool_field(object, stream, "pod_initialized", pod.pod_initialized);
    string_field(object, stream, "workload_kind", pod.workload_kind);
    string_field(object, stream, "workload_name", pod.workload_name);
    string_field(object, stream, "workload_uid", pod.workload_uid);
    bool_field(object, stream, "cpu_usage_available", pod.cpu_usage_available);
    double_field(object, stream, "cpu_usage_percent", pod.cpu_usage_percent);
    field(object, stream, "cpu_usage_usec", pod.cpu_usage_usec);
    bool_field(object, stream, "cpu_limit_available", pod.cpu_limit_available);
    bool_field(object, stream, "cpu_is_unlimited", pod.cpu_is_unlimited);
    double_field(object, stream, "cpu_limit_cores", pod.cpu_limit_cores);
    field(object, stream, "throttled_periods_delta",
          pod.throttled_periods_delta);
    field(object, stream, "throttled_usec_delta", pod.throttled_usec_delta);
    field(object, stream, "memory_current_bytes", pod.memory_current_bytes);
    field(object, stream, "memory_max_bytes", pod.memory_max_bytes);
    bool_field(object, stream, "memory_limit_available",
               pod.memory_limit_available);
    bool_field(object, stream, "memory_is_unlimited", pod.memory_is_unlimited);
    bool_field(object, stream, "memory_usage_percent_available",
               pod.memory_usage_percent_available);
    double_field(object, stream, "memory_usage_percent",
                 pod.memory_usage_percent);
    field(object, stream, "memory_high_delta", pod.memory_high_delta);
    field(object, stream, "memory_max_delta", pod.memory_max_delta);
    field(object, stream, "oom_delta", pod.oom_delta);
    field(object, stream, "oom_kill_delta", pod.oom_kill_delta);
    bool_field(object, stream, "cpu_request_available",
               pod.cpu_request_available);
    double_field(object, stream, "cpu_request_cores", pod.cpu_request_cores);
    bool_field(object, stream, "kubernetes_cpu_limit_available",
               pod.kubernetes_cpu_limit_available);
    double_field(object, stream, "kubernetes_cpu_limit_cores",
                 pod.kubernetes_cpu_limit_cores);
    bool_field(object, stream, "memory_request_available",
               pod.memory_request_available);
    field(object, stream, "memory_request_bytes", pod.memory_request_bytes);
    bool_field(object, stream, "kubernetes_memory_limit_available",
               pod.kubernetes_memory_limit_available);
    field(object, stream, "kubernetes_memory_limit_bytes",
          pod.kubernetes_memory_limit_bytes);
    bool_field(object, stream, "all_containers_ready",
               pod.all_containers_ready);
    field(object, stream, "restart_count", pod.restart_count);
    field(object, stream, "container_count", pod.container_count);
    object.key("container_ids");
    write_string_array(stream, pod.container_ids);
    object.key("container_names");
    write_string_array(stream, pod.container_names);
    object.key("process_ids");
    write_int_array(stream, pod.process_ids);
    object.key("lifecycle_reasons");
    write_string_array(stream, pod.lifecycle_reasons);
}

void write_deployment(std::ostringstream& stream,
                      const DeploymentMetric& deployment) {
    JsonObject object(stream);
    field(object, stream, "timestamp_unix_ms", deployment.timestamp_unix_ms);
    string_field(object, stream, "namespace", deployment.namespace_name);
    string_field(object, stream, "deployment_name",
                 deployment.deployment_name);
    string_field(object, stream, "deployment_uid", deployment.deployment_uid);
    field(object, stream, "generation", deployment.generation);
    field(object, stream, "observed_generation",
          deployment.observed_generation);
    field(object, stream, "desired_replicas", deployment.desired_replicas);
    field(object, stream, "ready_replicas", deployment.ready_replicas);
    field(object, stream, "available_replicas", deployment.available_replicas);
    field(object, stream, "unavailable_replicas",
          deployment.unavailable_replicas);
    field(object, stream, "updated_replicas", deployment.updated_replicas);
    field(object, stream, "observed_pod_count",
          deployment.observed_pod_count);
    bool_field(object, stream, "cpu_usage_available",
               deployment.cpu_usage_available);
    double_field(object, stream, "cpu_usage_percent",
                 deployment.cpu_usage_percent);
    field(object, stream, "cpu_usage_usec", deployment.cpu_usage_usec);
    field(object, stream, "memory_current_bytes",
          deployment.memory_current_bytes);
    field(object, stream, "throttled_periods_delta",
          deployment.throttled_periods_delta);
    field(object, stream, "throttled_usec_delta",
          deployment.throttled_usec_delta);
    field(object, stream, "memory_high_delta", deployment.memory_high_delta);
    field(object, stream, "memory_max_delta", deployment.memory_max_delta);
    field(object, stream, "oom_delta", deployment.oom_delta);
    field(object, stream, "oom_kill_delta", deployment.oom_kill_delta);
    field(object, stream, "restart_count", deployment.restart_count);
    bool_field(object, stream, "cpu_request_available",
               deployment.cpu_request_available);
    double_field(object, stream, "cpu_request_cores",
                 deployment.cpu_request_cores);
    bool_field(object, stream, "cpu_limit_available",
               deployment.cpu_limit_available);
    double_field(object, stream, "cpu_limit_cores",
                 deployment.cpu_limit_cores);
    bool_field(object, stream, "memory_request_available",
               deployment.memory_request_available);
    field(object, stream, "memory_request_bytes",
          deployment.memory_request_bytes);
    bool_field(object, stream, "memory_limit_available",
               deployment.memory_limit_available);
    field(object, stream, "memory_limit_bytes", deployment.memory_limit_bytes);
    object.key("pod_names");
    write_string_array(stream, deployment.pod_names);
}

void write_event(std::ostringstream& stream, const KubernetesEvent& event) {
    JsonObject object(stream);
    string_field(object, stream, "namespace", event.namespace_name);
    string_field(object, stream, "event_type", event.event_type);
    string_field(object, stream, "reason", event.reason);
    string_field(object, stream, "object_kind", event.object_kind);
    string_field(object, stream, "object_name", event.object_name);
    string_field(object, stream, "object_uid", event.object_uid);
    field(object, stream, "count", event.count);
    string_field(object, stream, "first_timestamp", event.first_timestamp);
    string_field(object, stream, "last_timestamp", event.last_timestamp);
    string_field(object, stream, "message", event.message);
}

template <typename Value, typename Writer>
void write_array(std::ostringstream& stream,
                 const std::vector<Value>& values,
                 Writer writer) {
    stream << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) stream << ',';
        writer(stream, values[index]);
    }
    stream << ']';
}

}  // namespace

std::string format_snapshot_as_json(const TelemetrySnapshot& snapshot) {
    std::ostringstream stream;
    {
        JsonObject object(stream);
        object.key("node");
        write_node(stream, snapshot.node);
        bool_field(object, stream, "cgroup_v2_available",
                   snapshot.cgroups.cgroup_v2_available);
        bool_field(object, stream, "kubernetes_metadata_available",
                   snapshot.kubernetes_metadata_available);
        object.key("processes");
        write_array(stream, snapshot.processes, write_process);
        object.key("top_cpu_processes");
        write_array(stream, snapshot.top_cpu_processes, write_process);
        object.key("top_memory_processes");
        write_array(stream, snapshot.top_memory_processes, write_process);
        object.key("containers");
        write_array(stream, snapshot.containers, write_container);
        object.key("pods");
        write_array(stream, snapshot.pods, write_pod);
        object.key("deployments");
        write_array(stream, snapshot.deployments, write_deployment);
        object.key("kubernetes_events");
        write_array(stream, snapshot.kubernetes_events, write_event);
    }
    return stream.str();
}
