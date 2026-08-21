#ifndef MEM_COLLECTOR_H
#define MEM_COLLECTOR_H

#include <cstdint>

struct MemorySample {
    std::uint64_t mem_total_kb = 0;
    std::uint64_t mem_available_kb = 0;
    std::uint64_t mem_free_kb = 0;
    std::uint64_t buffers_kb = 0;
    std::uint64_t cached_kb = 0;
    std::uint64_t swap_total_kb = 0;
    std::uint64_t swap_free_kb = 0;
};

class MemoryCollector {
public:
    MemorySample read_sample() const;
    double calculate_utilization(const MemorySample& sample) const;
};

#endif
