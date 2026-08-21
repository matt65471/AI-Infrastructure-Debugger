#include "mem_collector.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void set_memory_field(MemorySample& sample,
                      const std::string& field_name,
                      std::uint64_t value) {
    if (field_name == "MemTotal") {
        sample.mem_total_kb = value;
    } else if (field_name == "MemAvailable") {
        sample.mem_available_kb = value;
    } else if (field_name == "MemFree") {
        sample.mem_free_kb = value;
    } else if (field_name == "Buffers") {
        sample.buffers_kb = value;
    } else if (field_name == "Cached") {
        sample.cached_kb = value;
    } else if (field_name == "SwapTotal") {
        sample.swap_total_kb = value;
    } else if (field_name == "SwapFree") {
        sample.swap_free_kb = value;
    }
}

}  // namespace

MemorySample MemoryCollector::read_sample() const {
    std::ifstream proc_meminfo("/proc/meminfo");
    if (!proc_meminfo.is_open()) {
        throw std::runtime_error("failed to open /proc/meminfo");
    }

    std::string line;
    MemorySample sample;

    while (std::getline(proc_meminfo, line)) {
        std::istringstream stream(line);
        std::string field_name;
        std::uint64_t value = 0;
        std::string unit;

        stream >> field_name >> value >> unit;
        if (!stream) {
            continue;
        }

        if (!field_name.empty() && field_name.back() == ':') {
            field_name.pop_back();
        }

        set_memory_field(sample, field_name, value);
    }

    if (sample.mem_total_kb == 0 || sample.mem_available_kb == 0) {
        throw std::runtime_error("failed to parse required fields from /proc/meminfo");
    }

    return sample;
}

double MemoryCollector::calculate_utilization(const MemorySample& sample) const {
    if (sample.mem_total_kb == 0) {
        return 0.0;
    }

    const double used_kb = static_cast<double>(sample.mem_total_kb) -
                           static_cast<double>(sample.mem_available_kb);
    return 100.0 * (used_kb / static_cast<double>(sample.mem_total_kb));
}
