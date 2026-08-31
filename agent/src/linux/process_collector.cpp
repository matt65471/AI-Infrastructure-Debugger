#include "process_collector.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

bool is_numeric_name(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    for (char character : name) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> split_fields(const std::string& value) {
    std::istringstream stream(value);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field) {
        fields.push_back(field);
    }
    return fields;
}

bool read_stat_file(int pid, ProcessSample& sample) {
    std::ifstream stat_file("/proc/" + std::to_string(pid) + "/stat");
    if (!stat_file.is_open()) {
        return false;
    }

    std::string content;
    std::getline(stat_file, content);

    const std::size_t open_paren = content.find('(');
    const std::size_t close_paren = content.rfind(')');
    if (open_paren == std::string::npos || close_paren == std::string::npos ||
        close_paren <= open_paren) {
        return false;
    }

    sample.pid = pid;
    sample.name = content.substr(open_paren + 1, close_paren - open_paren - 1);

    const std::string after_name = content.substr(close_paren + 2);
    const std::vector<std::string> fields = split_fields(after_name);
    if (fields.size() <= 21) {
        return false;
    }

    sample.state = fields[0].empty() ? '?' : fields[0][0];
    sample.cpu_ticks = std::stoull(fields[11]) + std::stoull(fields[12]);
    sample.thread_count = std::stoull(fields[17]);
    sample.virtual_memory_kb = std::stoull(fields[20]) / 1024;

    const long page_size = sysconf(_SC_PAGESIZE);
    const std::uint64_t resident_pages = std::stoll(fields[21]) < 0
                                             ? 0
                                             : std::stoull(fields[21]);
    sample.resident_memory_kb =
        resident_pages * static_cast<std::uint64_t>(page_size) / 1024;

    return true;
}

void read_status_file(int pid, ProcessSample& sample) {
    std::ifstream status_file("/proc/" + std::to_string(pid) + "/status");
    if (!status_file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(status_file, line)) {
        std::istringstream stream(line);
        std::string field_name;
        std::uint64_t value = 0;
        std::string unit;

        stream >> field_name >> value >> unit;
        if (!stream) {
            continue;
        }

        if (field_name == "VmRSS:") {
            sample.resident_memory_kb = value;
        } else if (field_name == "VmSize:") {
            sample.virtual_memory_kb = value;
        } else if (field_name == "Threads:") {
            sample.thread_count = value;
        }
    }
}

void read_io_file(int pid, ProcessSample& sample) {
    std::ifstream io_file("/proc/" + std::to_string(pid) + "/io");
    if (!io_file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(io_file, line)) {
        std::istringstream stream(line);
        std::string field_name;
        std::uint64_t value = 0;
        stream >> field_name >> value;
        if (!stream) {
            continue;
        }

        if (field_name == "read_bytes:") {
            sample.read_bytes = value;
        } else if (field_name == "write_bytes:") {
            sample.write_bytes = value;
        }
    }
}

}  // namespace

ProcessCollectionSample ProcessCollector::read_sample() const {
    ProcessCollectionSample collection;
    std::error_code error;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(
             "/proc",
             std::filesystem::directory_options::skip_permission_denied,
             error)) {
        if (error) {
            break;
        }

        if (!entry.is_directory(error) || error) {
            error.clear();
            continue;
        }

        const std::string name = entry.path().filename().string();
        if (!is_numeric_name(name)) {
            continue;
        }

        const int pid = std::stoi(name);
        ProcessSample sample;
        try {
            if (!read_stat_file(pid, sample)) {
                continue;
            }

            read_status_file(pid, sample);
            read_io_file(pid, sample);
        } catch (const std::exception&) {
            continue;
        }

        collection.processes.push_back(sample);
    }

    return collection;
}
