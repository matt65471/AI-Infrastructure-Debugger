#ifndef NETWORK_COLLECTOR_H
#define NETWORK_COLLECTOR_H

#include <cstdint>
#include <string>
#include <vector>

struct NetworkInterfaceSample {
    std::string name;
    std::uint64_t rx_bytes = 0;
    std::uint64_t rx_packets = 0;
    std::uint64_t rx_errors = 0;
    std::uint64_t rx_drops = 0;
    std::uint64_t tx_bytes = 0;
    std::uint64_t tx_packets = 0;
    std::uint64_t tx_errors = 0;
    std::uint64_t tx_drops = 0;
};

struct NetworkSample {
    std::vector<NetworkInterfaceSample> interfaces;
};

class NetworkCollector {
public:
    NetworkSample read_sample() const;
    std::uint64_t calculate_rx_bytes_per_second(
        const NetworkSample& previous,
        const NetworkSample& current) const;
    std::uint64_t calculate_tx_bytes_per_second(
        const NetworkSample& previous,
        const NetworkSample& current) const;
};

#endif
