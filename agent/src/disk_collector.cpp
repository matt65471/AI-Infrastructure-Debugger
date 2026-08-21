#include "disk_collector.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool is_virtual_device(const std::string& name) {
    return starts_with(name, "loop") || starts_with(name, "ram") ||
           starts_with(name, "zram");
}

bool is_whole_block_device(const std::string& name) {
    std::error_code error;
    return std::filesystem::exists("/sys/block/" + name, error);
}

}  // namespace

DiskSample DiskCollector::read_sample() const {
    std::ifstream proc_diskstats("/proc/diskstats");
    if (!proc_diskstats.is_open()) {
        throw std::runtime_error("failed to open /proc/diskstats");
    }

    DiskSample sample;
    std::string line;
    while (std::getline(proc_diskstats, line)) {
        std::istringstream stream(line);
        int major = 0;
        int minor = 0;
        DiskDeviceSample device;
        std::uint64_t reads_merged = 0;
        std::uint64_t read_time_ms = 0;
        std::uint64_t writes_merged = 0;
        std::uint64_t write_time_ms = 0;
        std::uint64_t io_in_progress = 0;
        std::uint64_t weighted_io_time_ms = 0;

        stream >> major >> minor >> device.name >> device.reads_completed >>
            reads_merged >> device.sectors_read >> read_time_ms >>
            device.writes_completed >> writes_merged >>
            device.sectors_written >> write_time_ms >> io_in_progress >>
            device.io_time_ms >> weighted_io_time_ms;

        if (!stream) {
            continue;
        }

        if (!is_virtual_device(device.name) && is_whole_block_device(device.name)) {
            sample.devices.push_back(device);
        }
    }

    return sample;
}
