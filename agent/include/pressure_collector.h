#ifndef PRESSURE_COLLECTOR_H
#define PRESSURE_COLLECTOR_H

#include <cstdint>
#include <filesystem>

struct PressureValues {
    double avg10 = 0.0;
    double avg60 = 0.0;
    double avg300 = 0.0;
    std::uint64_t total_usec = 0;
};

struct ResourcePressure {
    bool available = false;
    PressureValues some;
    bool full_available = false;
    PressureValues full;
};

struct PressureSample {
    ResourcePressure cpu;
    ResourcePressure memory;
    ResourcePressure io;
    bool load_average_available = false;
    double load_average_1m = 0.0;
    double load_average_5m = 0.0;
    double load_average_15m = 0.0;
    std::uint64_t runnable_processes = 0;
    std::uint64_t total_processes = 0;
};

class PressureCollector {
public:
    explicit PressureCollector(std::filesystem::path proc_root = "/proc");
    PressureSample read_sample() const;

private:
    std::filesystem::path proc_root_;
};

#endif
