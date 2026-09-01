#include "cgroup_collector.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << content;
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main() {
    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path test_root =
        std::filesystem::temp_directory_path() /
        ("cgroup_collector_test_" + std::to_string(unique_suffix));
    const std::filesystem::path proc_root = test_root / "proc";
    const std::filesystem::path cgroup_root = test_root / "cgroup";
    const std::string container_id(64, 'a');
    const std::string cgroup_path =
        "/kubepods.slice/kubepods-burstable.slice/"
        "kubepods-burstable-pod123.slice/cri-containerd-" +
        container_id + ".scope";
    const std::filesystem::path cgroup_directory =
        cgroup_root / std::filesystem::path(cgroup_path).relative_path();

    write_file(cgroup_root / "cgroup.controllers", "cpu memory io\n");
    write_file(proc_root / "101/cgroup", "0::" + cgroup_path + "\n");
    write_file(proc_root / "202/cgroup", "0::/user.slice/example.service\n");
    write_file(cgroup_directory / "cpu.stat",
               "usage_usec 1200\nuser_usec 800\nsystem_usec 400\n"
               "nr_periods 10\nnr_throttled 2\nthrottled_usec 75\n");
    write_file(cgroup_directory / "memory.current", "4096\n");
    write_file(cgroup_directory / "memory.max", "8192\n");
    write_file(cgroup_directory / "memory.events",
               "low 1\nhigh 2\nmax 3\noom 4\noom_kill 5\n");
    write_file(cgroup_directory / "cgroup.procs", "101\n102\n");

    const CgroupCollector collector(proc_root, cgroup_root);
    const CgroupCollectionSample result = collector.read_sample({101, 202});

    bool passed = true;
    passed &= expect(result.cgroup_v2_available, "detects cgroup v2");
    passed &= expect(result.kubernetes_cgroups.size() == 1,
                     "filters non-Kubernetes cgroups");
    if (!result.kubernetes_cgroups.empty()) {
        const CgroupSample& sample = result.kubernetes_cgroups.front();
        passed &= expect(sample.path == cgroup_path, "preserves cgroup path");
        passed &= expect(sample.container_id == container_id,
                         "extracts container ID");
        passed &= expect(sample.cpu.usage_usec == 1200,
                         "reads cumulative CPU usage");
        passed &= expect(sample.cpu.throttled_usec == 75,
                         "reads throttled CPU time");
        passed &= expect(sample.memory_current_bytes == 4096,
                         "reads current memory");
        passed &= expect(sample.memory_max_bytes == 8192,
                         "reads memory limit");
        passed &= expect(sample.memory_events.oom_kill == 5,
                         "reads OOM kill count");
        passed &= expect(sample.process_ids == std::vector<int>({101, 102}),
                         "reads cgroup member PIDs");
    }

    std::filesystem::remove_all(test_root);
    return passed ? 0 : 1;
}
