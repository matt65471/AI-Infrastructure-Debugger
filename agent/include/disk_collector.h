#ifndef DISK_COLLECTOR_H
#define DISK_COLLECTOR_H

#include <cstdint>
#include <string>
#include <vector>

struct DiskDeviceSample {
    std::string name;
    std::uint64_t reads_completed = 0;
    std::uint64_t writes_completed = 0;
    std::uint64_t sectors_read = 0;
    std::uint64_t sectors_written = 0;
    std::uint64_t io_time_ms = 0;
};

struct DiskSample {
    std::vector<DiskDeviceSample> devices;
};

class DiskCollector {
public:
    DiskSample read_sample() const;
};

#endif
