#include "network_collector.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::uint64_t aggregate_rx_bytes(const NetworkSample& sample) {
    std::uint64_t total = 0;
    for (const NetworkInterfaceSample& interface : sample.interfaces) {
        if (interface.name != "lo") {
            total += interface.rx_bytes;
        }
    }
    return total;
}

std::uint64_t aggregate_tx_bytes(const NetworkSample& sample) {
    std::uint64_t total = 0;
    for (const NetworkInterfaceSample& interface : sample.interfaces) {
        if (interface.name != "lo") {
            total += interface.tx_bytes;
        }
    }
    return total;
}

}  // namespace

NetworkSample NetworkCollector::read_sample() const {
    std::ifstream proc_net_dev("/proc/net/dev");
    if (!proc_net_dev.is_open()) {
        throw std::runtime_error("failed to open /proc/net/dev");
    }

    std::string line;
    std::getline(proc_net_dev, line);
    std::getline(proc_net_dev, line);

    NetworkSample sample;
    while (std::getline(proc_net_dev, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        NetworkInterfaceSample interface_sample;
        interface_sample.name = trim(line.substr(0, colon));

        std::istringstream counters(line.substr(colon + 1));
        std::uint64_t rx_fifo = 0;
        std::uint64_t rx_frame = 0;
        std::uint64_t rx_compressed = 0;
        std::uint64_t rx_multicast = 0;
        std::uint64_t tx_fifo = 0;
        std::uint64_t tx_colls = 0;
        std::uint64_t tx_carrier = 0;
        std::uint64_t tx_compressed = 0;

        counters >> interface_sample.rx_bytes >>
            interface_sample.rx_packets >> interface_sample.rx_errors >>
            interface_sample.rx_drops >> rx_fifo >> rx_frame >>
            rx_compressed >> rx_multicast >> interface_sample.tx_bytes >>
            interface_sample.tx_packets >> interface_sample.tx_errors >>
            interface_sample.tx_drops >> tx_fifo >> tx_colls >>
            tx_carrier >> tx_compressed;

        if (!counters) {
            throw std::runtime_error("failed to parse interface line from /proc/net/dev");
        }

        sample.interfaces.push_back(interface_sample);
    }

    return sample;
}

std::uint64_t NetworkCollector::calculate_rx_bytes_per_second(
    const NetworkSample& previous,
    const NetworkSample& current) const {
    const std::uint64_t previous_bytes = aggregate_rx_bytes(previous);
    const std::uint64_t current_bytes = aggregate_rx_bytes(current);
    if (current_bytes < previous_bytes) {
        return 0;
    }
    return current_bytes - previous_bytes;
}

std::uint64_t NetworkCollector::calculate_tx_bytes_per_second(
    const NetworkSample& previous,
    const NetworkSample& current) const {
    const std::uint64_t previous_bytes = aggregate_tx_bytes(previous);
    const std::uint64_t current_bytes = aggregate_tx_bytes(current);
    if (current_bytes < previous_bytes) {
        return 0;
    }
    return current_bytes - previous_bytes;
}
