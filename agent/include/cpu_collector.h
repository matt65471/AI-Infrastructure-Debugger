#ifndef CPU_COLLECTOR_H
#define CPU_COLLECTOR_H

#include <cstdint>

struct CpuSample {
    std::uint64_t user = 0;
    std::uint64_t nice = 0;
    std::uint64_t system = 0;
    std::uint64_t idle = 0;
    std::uint64_t iowait = 0;
    std::uint64_t irq = 0;
    std::uint64_t softirq = 0;
    std::uint64_t steal = 0;
};

class CpuCollector {
public:
    CpuSample read_sample() const;
    double calculate_utilization(const CpuSample& previous,
                                 const CpuSample& current) const;
};

#endif
