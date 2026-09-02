#ifndef CGROUP_COLLECTOR_H
#define CGROUP_COLLECTOR_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct CgroupCpuStat {
    std::uint64_t usage_usec = 0;
    std::uint64_t user_usec = 0;
    std::uint64_t system_usec = 0;
    std::uint64_t periods = 0;
    std::uint64_t throttled_periods = 0;
    std::uint64_t throttled_usec = 0;
};

struct CgroupMemoryEvents {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
    std::uint64_t max = 0;
    std::uint64_t oom = 0;
    std::uint64_t oom_kill = 0;
};

struct CgroupSample {
    std::string path;
    std::string container_id;
    CgroupCpuStat cpu;
    std::uint64_t cpu_quota_usec = 0;
    std::uint64_t cpu_period_usec = 0;
    bool cpu_limit_available = false;
    bool cpu_is_unlimited = false;
    std::uint64_t memory_current_bytes = 0;
    std::uint64_t memory_max_bytes = 0;
    bool memory_is_unlimited = false;
    CgroupMemoryEvents memory_events;
    std::vector<int> process_ids;
};

struct CgroupCollectionSample {
    bool cgroup_v2_available = false;
    std::vector<CgroupSample> kubernetes_cgroups;
};

class CgroupCollector {
public:
    CgroupCollector(
        std::filesystem::path proc_root = "/proc",
        std::filesystem::path cgroup_root = "/sys/fs/cgroup");

    CgroupCollectionSample read_sample(const std::vector<int>& process_ids) const;

private:
    std::filesystem::path proc_root_;
    std::filesystem::path cgroup_root_;
};

#endif
