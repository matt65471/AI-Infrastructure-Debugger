#include "telemetry_collector.h"
#include "json_formatter.h"
#include "infrastructure_exporter.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct OutputOptions {
    bool json_stdout = false;
    bool exporter_option_seen = false;
    InfrastructureExporterOptions exporter;
};

std::atomic<bool> stop_requested{false};

void request_stop(int) {
    stop_requested.store(true);
}

OutputOptions parse_options(int argc, char* argv[]) {
    OutputOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--json") {
            options.json_stdout = true;
        } else if (argument == "--ingest-url" && index + 1 < argc) {
            options.exporter_option_seen = true;
            options.exporter.ingest_url = argv[++index];
        } else if (argument == "--token-file" && index + 1 < argc) {
            options.exporter_option_seen = true;
            options.exporter.token_file = argv[++index];
        } else if (argument == "--spool-dir" && index + 1 < argc) {
            options.exporter_option_seen = true;
            options.exporter.spool_directory = argv[++index];
        } else if (argument == "--spool-max-bytes" && index + 1 < argc) {
            options.exporter_option_seen = true;
            options.exporter.spool_max_bytes = std::stoull(argv[++index]);
        } else {
            throw std::runtime_error(
                "usage: telemetry_agent [--json | --ingest-url URL "
                "--token-file PATH --spool-dir PATH "
                "[--spool-max-bytes BYTES]]");
        }
    }
    const bool uses_exporter = options.exporter_option_seen;
    if (options.json_stdout && uses_exporter) {
        throw std::runtime_error(
            "--json and HTTP ingestion options cannot be used together");
    }
    if (uses_exporter && (options.exporter.ingest_url.empty() ||
                         options.exporter.token_file.empty() ||
                         options.exporter.spool_directory.empty() ||
                         options.exporter.spool_max_bytes == 0)) {
        throw std::runtime_error(
            "--ingest-url, --token-file, and --spool-dir must be used together");
    }
    return options;
}

std::string format_process_list(const std::vector<ProcessMetric>& processes,
                                bool include_cpu) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < processes.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }

        const ProcessMetric& process = processes[index];
        stream << process.pid << ':' << process.name << ':';
        if (include_cpu) {
            stream << std::fixed << std::setprecision(2)
                   << process.cpu_usage_percent;
        } else {
            stream << process.resident_memory_kb;
        }
    }
    stream << ']';
    return stream.str();
}

std::string format_pid_list(const std::vector<int>& process_ids) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < process_ids.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << process_ids[index];
    }
    stream << ']';
    return stream.str();
}

std::string short_container_id(const std::string& container_id) {
    constexpr std::size_t kShortIdLength = 12;
    return container_id.size() <= kShortIdLength
               ? container_id
               : container_id.substr(0, kShortIdLength);
}

std::string format_string_list(const std::vector<std::string>& values) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << values[index];
    }
    stream << ']';
    return stream.str();
}

std::string format_container_id_list(
    const std::vector<std::string>& container_ids) {
    std::vector<std::string> short_ids;
    short_ids.reserve(container_ids.size());
    for (const std::string& container_id : container_ids) {
        short_ids.push_back(short_container_id(container_id));
    }
    return format_string_list(short_ids);
}

std::string format_pressure_values(const PressureValues& values) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2)
           << "{avg10=" << values.avg10
           << ",avg60=" << values.avg60
           << ",avg300=" << values.avg300
           << ",total_usec=" << values.total_usec << '}';
    return stream.str();
}

std::string format_pressure(const ResourcePressure& pressure) {
    if (!pressure.available && !pressure.full_available) {
        return "na";
    }
    std::ostringstream stream;
    stream << "{some=";
    if (pressure.available) {
        stream << format_pressure_values(pressure.some);
    } else {
        stream << "na";
    }
    stream << ",full=";
    if (pressure.full_available) {
        stream << format_pressure_values(pressure.full);
    } else {
        stream << "na";
    }
    stream << '}';
    return stream.str();
}

std::string format_container_list(
    const std::vector<ContainerMetric>& containers) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < containers.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }

        const ContainerMetric& container = containers[index];
        stream << "{container_id="
               << short_container_id(container.container_id)
               << ",path=" << container.cgroup_path
               << ",cpu_usage_percent=";
        if (container.cpu_usage_available) {
            stream << std::fixed << std::setprecision(2)
                   << container.cpu_usage_percent;
        } else {
            stream << "na";
        }
        stream << ",cpu_usage_usec=" << container.cpu_usage_usec
               << ",cpu_limit_cores=";
        if (container.cpu_is_unlimited) {
            stream << "max";
        } else if (container.cpu_limit_available) {
            stream << std::fixed << std::setprecision(3)
                   << container.cpu_limit_cores;
        } else {
            stream << "na";
        }
        stream
               << ",throttled_periods_delta="
               << container.throttled_periods_delta
               << ",throttled_usec_delta="
               << container.throttled_usec_delta
               << ",memory_current_bytes="
               << container.memory_current_bytes
               << ",memory_max=";
        if (container.memory_is_unlimited) {
            stream << "max";
        } else {
            stream << container.memory_max_bytes;
        }
        stream << ",memory_usage_percent=";
        if (container.memory_usage_percent_available) {
            stream << std::fixed << std::setprecision(2)
                   << container.memory_usage_percent;
        } else {
            stream << "na";
        }
        stream << ",memory_high_delta=" << container.memory_high_delta
               << ",memory_max_delta=" << container.memory_max_delta
               << ",oom_delta=" << container.oom_delta
               << ",oom_kill_delta=" << container.oom_kill_delta
               << ",pids=" << format_pid_list(container.process_ids);
        if (container.kubernetes_identity_available) {
            stream << ",kubernetes={node=" << container.node_name
                   << ",namespace=" << container.namespace_name
                   << ",pod=" << container.pod_name
                   << ",pod_uid=" << container.pod_uid
                   << ",container=" << container.container_name
                   << ",image=" << container.image
                   << ",phase=" << container.pod_phase
                   << ",qos=" << container.pod_qos_class
                   << ",pod_ready="
                   << (container.pod_ready ? "true" : "false")
                   << ",scheduled="
                   << (container.pod_scheduled ? "true" : "false")
                   << ",initialized="
                   << (container.pod_initialized ? "true" : "false")
                   << ",ready="
                   << (container.container_ready ? "true" : "false")
                   << ",restarts=" << container.restart_count
                   << ",state=" << container.container_state
                   << ",reason=" << container.state_reason
                   << ",exit_code=" << container.exit_code
                   << ",last_reason="
                   << container.last_termination_reason
                   << ",last_exit_code=" << container.last_exit_code
                   << ",started_at=" << container.started_at
                   << ",finished_at=" << container.finished_at
                   << ",last_finished_at=" << container.last_finished_at
                   << ",cpu_request_cores=";
            if (container.cpu_request_available) {
                stream << container.cpu_request_cores;
            } else {
                stream << "na";
            }
            stream << ",cpu_limit_cores=";
            if (container.kubernetes_cpu_limit_available) {
                stream << container.kubernetes_cpu_limit_cores;
            } else {
                stream << "na";
            }
            stream << ",memory_request_bytes=";
            if (container.memory_request_available) {
                stream << container.memory_request_bytes;
            } else {
                stream << "na";
            }
            stream << ",memory_limit_bytes=";
            if (container.kubernetes_memory_limit_available) {
                stream << container.kubernetes_memory_limit_bytes;
            } else {
                stream << "na";
            }
            stream
                   << ",workload=" << container.workload_kind << '/'
                   << container.workload_name << '}';
        } else {
            stream << ",kubernetes=unmatched";
        }
        stream << '}';
    }
    stream << ']';
    return stream.str();
}

std::string format_pod_list(const std::vector<PodMetric>& pods) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < pods.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }

        const PodMetric& pod = pods[index];
        stream << "{node=" << pod.node_name
               << ",namespace=" << pod.namespace_name
               << ",pod=" << pod.pod_name
               << ",pod_uid=" << pod.pod_uid
               << ",workload=" << pod.workload_kind << '/'
               << pod.workload_name
               << ",phase=" << pod.pod_phase
               << ",qos=" << pod.pod_qos_class
               << ",pod_ready=" << (pod.pod_ready ? "true" : "false")
               << ",scheduled="
               << (pod.pod_scheduled ? "true" : "false")
               << ",initialized="
               << (pod.pod_initialized ? "true" : "false")
               << ",ready="
               << (pod.all_containers_ready ? "true" : "false")
               << ",container_count=" << pod.container_count
               << ",container_names="
               << format_string_list(pod.container_names)
               << ",container_ids="
               << format_container_id_list(pod.container_ids)
               << ",cpu_usage_percent=";
        if (pod.cpu_usage_available) {
            stream << std::fixed << std::setprecision(2)
                   << pod.cpu_usage_percent;
        } else {
            stream << "na";
        }
        stream << ",cpu_usage_usec=" << pod.cpu_usage_usec
               << ",cpu_limit_cores=";
        if (pod.cpu_is_unlimited) {
            stream << "max";
        } else if (pod.cpu_limit_available) {
            stream << pod.cpu_limit_cores;
        } else {
            stream << "na";
        }
        stream
               << ",throttled_periods_delta="
               << pod.throttled_periods_delta
               << ",throttled_usec_delta=" << pod.throttled_usec_delta
               << ",memory_current_bytes=" << pod.memory_current_bytes
               << ",memory_max=";
        if (pod.memory_is_unlimited) {
            stream << "max";
        } else if (pod.memory_limit_available) {
            stream << pod.memory_max_bytes;
        } else {
            stream << "na";
        }
        stream << ",memory_usage_percent=";
        if (pod.memory_usage_percent_available) {
            stream << std::fixed << std::setprecision(2)
                   << pod.memory_usage_percent;
        } else {
            stream << "na";
        }
        stream << ",memory_high_delta=" << pod.memory_high_delta
               << ",memory_max_delta=" << pod.memory_max_delta
               << ",oom_delta=" << pod.oom_delta
               << ",oom_kill_delta=" << pod.oom_kill_delta
               << ",restarts=" << pod.restart_count
               << ",cpu_request_cores=";
        if (pod.cpu_request_available) {
            stream << pod.cpu_request_cores;
        } else {
            stream << "na";
        }
        stream << ",configured_cpu_limit_cores=";
        if (pod.kubernetes_cpu_limit_available) {
            stream << pod.kubernetes_cpu_limit_cores;
        } else {
            stream << "na";
        }
        stream << ",memory_request_bytes=";
        if (pod.memory_request_available) {
            stream << pod.memory_request_bytes;
        } else {
            stream << "na";
        }
        stream << ",configured_memory_limit_bytes=";
        if (pod.kubernetes_memory_limit_available) {
            stream << pod.kubernetes_memory_limit_bytes;
        } else {
            stream << "na";
        }
        stream << ",lifecycle_reasons="
               << format_string_list(pod.lifecycle_reasons)
               << ",pids=" << format_pid_list(pod.process_ids) << '}';
    }
    stream << ']';
    return stream.str();
}

void append_optional_double(std::ostringstream& stream,
                            bool available,
                            double value) {
    if (available) {
        stream << std::fixed << std::setprecision(3) << value;
    } else {
        stream << "na";
    }
}

void append_optional_bytes(std::ostringstream& stream,
                           bool available,
                           std::uint64_t value) {
    if (available) {
        stream << value;
    } else {
        stream << "na";
    }
}

std::string format_deployment_list(
    const std::vector<DeploymentMetric>& deployments) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < deployments.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }

        const DeploymentMetric& deployment = deployments[index];
        stream << "{namespace=" << deployment.namespace_name
               << ",deployment=" << deployment.deployment_name
               << ",uid=" << deployment.deployment_uid
               << ",generation=" << deployment.generation
               << ",observed_generation="
               << deployment.observed_generation
               << ",desired_replicas=" << deployment.desired_replicas
               << ",ready_replicas=" << deployment.ready_replicas
               << ",available_replicas=" << deployment.available_replicas
               << ",unavailable_replicas="
               << deployment.unavailable_replicas
               << ",updated_replicas=" << deployment.updated_replicas
               << ",observed_pods=" << deployment.observed_pod_count
               << ",cpu_usage_percent=";
        append_optional_double(stream, deployment.cpu_usage_available,
                               deployment.cpu_usage_percent);
        stream << ",cpu_usage_usec=" << deployment.cpu_usage_usec
               << ",memory_current_bytes="
               << deployment.memory_current_bytes
               << ",throttled_periods_delta="
               << deployment.throttled_periods_delta
               << ",throttled_usec_delta="
               << deployment.throttled_usec_delta
               << ",memory_high_delta=" << deployment.memory_high_delta
               << ",memory_max_delta=" << deployment.memory_max_delta
               << ",oom_delta=" << deployment.oom_delta
               << ",oom_kill_delta=" << deployment.oom_kill_delta
               << ",restarts=" << deployment.restart_count
               << ",cpu_request_cores=";
        append_optional_double(stream, deployment.cpu_request_available,
                               deployment.cpu_request_cores);
        stream << ",cpu_limit_cores=";
        append_optional_double(stream, deployment.cpu_limit_available,
                               deployment.cpu_limit_cores);
        stream << ",memory_request_bytes=";
        append_optional_bytes(stream, deployment.memory_request_available,
                              deployment.memory_request_bytes);
        stream << ",memory_limit_bytes=";
        append_optional_bytes(stream, deployment.memory_limit_available,
                              deployment.memory_limit_bytes);
        stream << ",pods=" << format_string_list(deployment.pod_names) << '}';
    }
    stream << ']';
    return stream.str();
}

std::string format_kubernetes_event_list(
    const std::vector<KubernetesEvent>& events) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < events.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }
        const KubernetesEvent& event = events[index];
        stream << "{namespace=" << event.namespace_name
               << ",type=" << event.event_type
               << ",reason=" << event.reason
               << ",object=" << event.object_kind << '/' << event.object_name
               << ",uid=" << event.object_uid
               << ",count=" << event.count
               << ",first=" << event.first_timestamp
               << ",last=" << event.last_timestamp
               << ",message=" << event.message << '}';
    }
    stream << ']';
    return stream.str();
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const OutputOptions options = parse_options(argc, argv);
        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        TelemetryCollector telemetry_collector;
        std::unique_ptr<InfrastructureExporter> exporter;
        std::thread exporter_thread;
        if (!options.exporter.ingest_url.empty()) {
            exporter = std::make_unique<InfrastructureExporter>(options.exporter);
            exporter_thread = std::thread([&exporter]() {
                exporter->run(stop_requested);
            });
        }

        try {
            while (!stop_requested.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (stop_requested.load()) {
                break;
            }

            const TelemetrySnapshot snapshot = telemetry_collector.collect();
            if (options.json_stdout) {
                std::cout << format_snapshot_as_json(snapshot) << '\n';
                continue;
            }
            if (exporter) {
                exporter->enqueue(
                    format_snapshot_as_json(snapshot),
                    snapshot.node.timestamp_unix_ms
                );
                continue;
            }
            const NodeMetric& node = snapshot.node;
            std::cout << "timestamp_unix_ms=" << node.timestamp_unix_ms
                      << " node=" << node.hostname
                      << " logical_cpu_count=" << node.logical_cpu_count
                      << " cpu_usage_percent=" << std::fixed
                      << std::setprecision(2) << node.cpu_usage_percent
                      << " memory_usage_percent="
                      << node.memory_usage_percent
                      << " memory_total_kb=" << node.memory_total_kb
                      << " memory_available_kb="
                      << node.memory_available_kb
                      << " swap_total_kb=" << node.swap_total_kb
                      << " swap_free_kb=" << node.swap_free_kb
                      << " network_rx_bytes_per_second="
                      << node.network_rx_bytes_per_second
                      << " network_tx_bytes_per_second="
                      << node.network_tx_bytes_per_second
                      << " tcp_retransmits_per_second="
                      << node.tcp_retransmits_per_second
                      << " tcp_in_segments_per_second="
                      << node.tcp_in_segments_per_second
                      << " tcp_out_segments_per_second="
                      << node.tcp_out_segments_per_second
                      << " tcp_reset_count_delta="
                      << node.tcp_reset_count_delta
                      << " tcp_listen_overflows_delta="
                      << node.tcp_listen_overflows_delta
                      << " tcp_listen_drops_delta="
                      << node.tcp_listen_drops_delta
                      << " tcp_timeouts_delta="
                      << node.tcp_timeouts_delta
                      << " disk_read_bytes_per_second="
                      << node.disk_read_bytes_per_second
                      << " disk_write_bytes_per_second="
                      << node.disk_write_bytes_per_second
                      << " disk_reads_per_second="
                      << node.disk_reads_per_second
                      << " disk_writes_per_second="
                      << node.disk_writes_per_second
                      << " disk_io_time_ms_delta="
                      << node.disk_io_time_ms_delta
                      << " load_average_1m=";
            if (node.pressure.load_average_available) {
                std::cout << node.pressure.load_average_1m
                          << " load_average_5m="
                          << node.pressure.load_average_5m
                          << " load_average_15m="
                          << node.pressure.load_average_15m
                          << " runnable_processes="
                          << node.pressure.runnable_processes
                          << " total_processes="
                          << node.pressure.total_processes;
            } else {
                std::cout << "na load_average_5m=na load_average_15m=na"
                          << " runnable_processes=na total_processes=na";
            }
            std::cout << " cpu_pressure="
                      << format_pressure(node.pressure.cpu)
                      << " memory_pressure="
                      << format_pressure(node.pressure.memory)
                      << " io_pressure="
                      << format_pressure(node.pressure.io);
            std::cout
                      << " top_cpu="
                      << format_process_list(snapshot.top_cpu_processes, true)
                      << " top_memory="
                      << format_process_list(snapshot.top_memory_processes, false)
                      << " cgroup_v2="
                      << (snapshot.cgroups.cgroup_v2_available ? "true" : "false")
                      << " kubernetes_metadata="
                      << (snapshot.kubernetes_metadata_available
                              ? "available"
                              : "unavailable")
                      << " containers="
                      << format_container_list(snapshot.containers)
                      << " pods=" << format_pod_list(snapshot.pods)
                      << " deployments="
                      << format_deployment_list(snapshot.deployments)
                      << " kubernetes_events="
                      << format_kubernetes_event_list(
                             snapshot.kubernetes_events)
                      << '\n';
            }
        } catch (...) {
            stop_requested.store(true);
            if (exporter_thread.joinable()) {
                exporter_thread.join();
            }
            throw;
        }
        if (exporter_thread.joinable()) {
            exporter_thread.join();
        }
    } catch (const std::exception& error) {
        std::cerr << "telemetry_agent error: " << error.what() << '\n';
        return 1;
    }
}
