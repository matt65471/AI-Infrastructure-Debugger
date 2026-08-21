#ifndef PROCESS_COLLECTOR_H
#define PROCESS_COLLECTOR_H

#include <cstdint>
#include <string>
#include <vector>

struct ProcessSample {
    int pid = 0;
    std::string name;
    char state = '?';
    std::uint64_t cpu_ticks = 0;
    std::uint64_t resident_memory_kb = 0;
    std::uint64_t virtual_memory_kb = 0;
    std::uint64_t thread_count = 0;
    std::uint64_t read_bytes = 0;
    std::uint64_t write_bytes = 0;
};

struct ProcessCollectionSample {
    std::vector<ProcessSample> processes;
};

class ProcessCollector {
public:
    ProcessCollectionSample read_sample() const;
};

#endif
