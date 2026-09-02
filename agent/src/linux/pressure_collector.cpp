#include "pressure_collector.h"

#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace {

void parse_pressure_values(std::istringstream& stream, PressureValues& values) {
    std::string field;
    while (stream >> field) {
        const std::size_t separator = field.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = field.substr(0, separator);
        const std::string value = field.substr(separator + 1);
        try {
            if (key == "avg10") {
                values.avg10 = std::stod(value);
            } else if (key == "avg60") {
                values.avg60 = std::stod(value);
            } else if (key == "avg300") {
                values.avg300 = std::stod(value);
            } else if (key == "total") {
                values.total_usec = std::stoull(value);
            }
        } catch (const std::exception&) {
            continue;
        }
    }
}

ResourcePressure read_pressure_file(const std::filesystem::path& path) {
    ResourcePressure pressure;
    std::ifstream file(path);
    if (!file.is_open()) {
        return pressure;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream stream(line);
        std::string type;
        stream >> type;
        if (type == "some") {
            pressure.available = true;
            parse_pressure_values(stream, pressure.some);
        } else if (type == "full") {
            pressure.full_available = true;
            parse_pressure_values(stream, pressure.full);
        }
    }
    return pressure;
}

void read_load_average(const std::filesystem::path& path,
                       PressureSample& sample) {
    std::ifstream file(path);
    std::string process_counts;
    file >> sample.load_average_1m >> sample.load_average_5m >>
        sample.load_average_15m >> process_counts;
    if (!file) {
        return;
    }

    const std::size_t separator = process_counts.find('/');
    if (separator == std::string::npos) {
        return;
    }
    try {
        sample.runnable_processes =
            std::stoull(process_counts.substr(0, separator));
        sample.total_processes =
            std::stoull(process_counts.substr(separator + 1));
        sample.load_average_available = true;
    } catch (const std::exception&) {
        sample.load_average_available = false;
    }
}

}  // namespace

PressureCollector::PressureCollector(std::filesystem::path proc_root)
    : proc_root_(std::move(proc_root)) {}

PressureSample PressureCollector::read_sample() const {
    PressureSample sample;
    sample.cpu = read_pressure_file(proc_root_ / "pressure/cpu");
    sample.memory = read_pressure_file(proc_root_ / "pressure/memory");
    sample.io = read_pressure_file(proc_root_ / "pressure/io");
    read_load_average(proc_root_ / "loadavg", sample);
    return sample;
}
