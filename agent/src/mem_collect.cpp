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

        ss >> word;

        if (sample.variables.find(word) != sample.variables.end()) {
            std::string temp = word;
            ss >> word;
            std::cout << temp << " " << word << std::endl;
            
        }
    }


}