#include "cpu_collector.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::uint64_t total_time(const CpuSample& sample) {
    return sample.user + sample.nice + sample.system + sample.idle +
           sample.iowait + sample.irq + sample.softirq + sample.steal;
}

std::uint64_t idle_time(const CpuSample& sample) {
    return sample.idle + sample.iowait;
}

}

CpuSample CpuCollector::read_sample() const {
    std::ifstream proc_stat("/proc/stat");
    if (!proc_stat.is_open()) {
        throw std::runtime_error("failed to open /proc/stat");
    }

    std::string line;
    std::getline(proc_stat, line);

    std::istringstream stream(line);
    std::string label;
    CpuSample sample;

    stream >> label >> sample.user >> sample.nice >> sample.system >>
        sample.idle >> sample.iowait >> sample.irq >> sample.softirq >>
        sample.steal;

    if (!stream || label != "cpu") {
        throw std::runtime_error("failed to parse aggregate cpu line from /proc/stat");
    }

    proc_stat.close();

    return sample;
}

double CpuCollector::calculate_utilization(const CpuSample& previous,
                                           const CpuSample& current) const {
    const std::uint64_t previous_total = total_time(previous);
    const std::uint64_t current_total = total_time(current);
    const std::uint64_t previous_idle = idle_time(previous);
    const std::uint64_t current_idle = idle_time(current);

    const std::uint64_t total_delta = current_total - previous_total;
    const std::uint64_t idle_delta = current_idle - previous_idle;

    if (total_delta == 0) {
        return 0.0;
    }

    return 100.0 * (1.0 - static_cast<double>(idle_delta) /
                              static_cast<double>(total_delta));
}