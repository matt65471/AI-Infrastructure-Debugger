#include "mem_collector.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <iostream>
#include <string>


MemSample MemCollector::read_sample() const {
    std::ifstream proc_meminfo("/proc/meminfo");
    if (!proc_meminfo.is_open()) {
        throw std::runtime_error("failed to open /proc/meminfo");
    }

    std::string line;
    MemSample sample;

    while (std::getline(proc_meminfo, line)) {
        std::istringstream ss(line);
        std::string word;
        uint64_t value;

        ss >> word;

        if (!word.empty() && word.back() == ':') {
            word.pop_back();
        }

        if (sample.tracked_info.find(word) != sample.tracked_info.end()) {
            ss >> value;

            sample.tracked_info[word] = value;

            std::cout << word << ": " << value << std::endl;
            
        }
    }
    return sample;
};

double MemCollector::calculate_memory_utilization(const MemSample& sample) const{
    double used = static_cast<double>(sample.tracked_info.at("MemTotal")) - static_cast<double>(sample.tracked_info.at("MemAvailable"));
    return 100.0 * (used / static_cast<double>(sample.tracked_info.at("MemTotal")));
};