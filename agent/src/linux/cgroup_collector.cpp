#include "cgroup_collector.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace {

bool is_kubernetes_cgroup(const std::string& path) {
    return path.find("kubepods") != std::string::npos;
}

std::string read_cgroup_path(const std::filesystem::path& proc_root, int pid) {
    std::ifstream file(proc_root / std::to_string(pid) / "cgroup");
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t first_colon = line.find(':');
        if (first_colon == std::string::npos) {
            continue;
        }

        const std::size_t second_colon = line.find(':', first_colon + 1);
        if (second_colon == std::string::npos) {
            continue;
        }

        const std::string hierarchy = line.substr(0, first_colon);
        const std::string controllers =
            line.substr(first_colon + 1, second_colon - first_colon - 1);
        if (hierarchy == "0" && controllers.empty()) {
            return line.substr(second_colon + 1);
        }
    }
    return {};
}

std::filesystem::path resolve_cgroup_path(
    const std::filesystem::path& root,
    const std::string& cgroup_path) {
    return root / std::filesystem::path(cgroup_path).relative_path();
}

std::uint64_t read_number(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::uint64_t value = 0;
    file >> value;
    return file ? value : 0;
}

void read_cpu_stat(const std::filesystem::path& path, CgroupCpuStat& stat) {
    std::ifstream file(path / "cpu.stat");
    std::string key;
    std::uint64_t value = 0;
    while (file >> key >> value) {
        if (key == "usage_usec") {
            stat.usage_usec = value;
        } else if (key == "user_usec") {
            stat.user_usec = value;
        } else if (key == "system_usec") {
            stat.system_usec = value;
        } else if (key == "nr_periods") {
            stat.periods = value;
        } else if (key == "nr_throttled") {
            stat.throttled_periods = value;
        } else if (key == "throttled_usec") {
            stat.throttled_usec = value;
        }
    }
}

void read_memory_max(const std::filesystem::path& path, CgroupSample& sample) {
    std::ifstream file(path / "memory.max");
    std::string value;
    file >> value;
    if (!file) {
        return;
    }

    if (value == "max") {
        sample.memory_is_unlimited = true;
        return;
    }

    try {
        sample.memory_max_bytes = std::stoull(value);
    } catch (const std::exception&) {
        sample.memory_max_bytes = 0;
    }
}

void read_memory_events(const std::filesystem::path& path,
                        CgroupMemoryEvents& events) {
    std::ifstream file(path / "memory.events");
    std::string key;
    std::uint64_t value = 0;
    while (file >> key >> value) {
        if (key == "low") {
            events.low = value;
        } else if (key == "high") {
            events.high = value;
        } else if (key == "max") {
            events.max = value;
        } else if (key == "oom") {
            events.oom = value;
        } else if (key == "oom_kill") {
            events.oom_kill = value;
        }
    }
}

std::vector<int> read_process_ids(const std::filesystem::path& path) {
    std::ifstream file(path / "cgroup.procs");
    std::vector<int> process_ids;
    int pid = 0;
    while (file >> pid) {
        process_ids.push_back(pid);
    }
    return process_ids;
}

bool is_hex_character(char character) {
    return std::isxdigit(static_cast<unsigned char>(character)) != 0;
}

std::string extract_container_id(const std::string& path) {
    constexpr std::size_t kContainerIdLength = 64;
    std::string result;
    std::size_t run_start = 0;
    std::size_t run_length = 0;

    for (std::size_t index = 0; index <= path.size(); ++index) {
        if (index < path.size() && is_hex_character(path[index])) {
            if (run_length == 0) {
                run_start = index;
            }
            ++run_length;
            continue;
        }

        if (run_length == kContainerIdLength) {
            result = path.substr(run_start, run_length);
        }
        run_length = 0;
    }

    return result;
}

}  // namespace

CgroupCollector::CgroupCollector(std::filesystem::path proc_root,
                                 std::filesystem::path cgroup_root)
    : proc_root_(std::move(proc_root)),
      cgroup_root_(std::move(cgroup_root)) {}

CgroupCollectionSample CgroupCollector::read_sample(
    const std::vector<int>& process_ids) const {
    CgroupCollectionSample collection;
    std::error_code error;
    collection.cgroup_v2_available =
        std::filesystem::exists(cgroup_root_ / "cgroup.controllers", error);
    if (!collection.cgroup_v2_available) {
        return collection;
    }

    std::map<std::string, std::vector<int>> discovered;
    for (int pid : process_ids) {
        const std::string cgroup_path = read_cgroup_path(proc_root_, pid);
        if (is_kubernetes_cgroup(cgroup_path)) {
            discovered[cgroup_path].push_back(pid);
        }
    }

    for (const auto& [path, discovered_processes] : discovered) {
        const std::filesystem::path filesystem_path =
            resolve_cgroup_path(cgroup_root_, path);
        error.clear();
        if (!std::filesystem::is_directory(filesystem_path, error) || error) {
            continue;
        }

        CgroupSample sample;
        sample.path = path;
        sample.container_id = extract_container_id(path);
        read_cpu_stat(filesystem_path, sample.cpu);
        sample.memory_current_bytes =
            read_number(filesystem_path / "memory.current");
        read_memory_max(filesystem_path, sample);
        read_memory_events(filesystem_path, sample.memory_events);
        sample.process_ids = read_process_ids(filesystem_path);
        if (sample.process_ids.empty()) {
            sample.process_ids = discovered_processes;
        }
        std::sort(sample.process_ids.begin(), sample.process_ids.end());

        collection.kubernetes_cgroups.push_back(std::move(sample));
    }

    return collection;
}
