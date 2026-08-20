#ifndef MEM_COLLECTOR_H
#define MEM_COLLECTOR_H

#include <cstdint>
#include <unordered_map>
#include <string>

struct MemSample {
    std::unordered_map<std::string, uint64_t> tracked_info = {
        {"MemTotal", 0},
        {"MemAvailable", 0},
        {"MemFree", 0},
        {"Buffers", 0},
        {"Cached", 0},
        {"SwapTota", 0},
        {"SwapFree", 0}
    };
};

class MemCollector {
public:
    MemSample read_sample() const;
    double calculate_memory_utilization(const MemSample& sample) const;
};

#endif