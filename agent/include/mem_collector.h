#ifndef MEM_COLLECTOR_H
#define MEM_COLLECTOR_H

#include <cstdint>

struct MemSample {
    std::uint64_t MemTotal = 0;
    std::uint64_t MemAvailable = 0;
    std::uint64_t MemFree = 0;
    std::uint64_t Buffers = 0;
    std::uint64_t Cached = 0;
    std::uint64_t SwapTotal = 0;
    std::uint64_t SwapFree = 0;
};

class MemCollector {
public:
    MemSample read_sample() const;
    double calculate_utilization(const MemSample& sample) const;
};

#endif