#include "tcp_collector.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using CounterMap = std::unordered_map<std::string, std::uint64_t>;

std::vector<std::string> split_fields(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field) {
        fields.push_back(field);
    }
    return fields;
}

CounterMap parse_counter_rows(const std::vector<std::string>& header,
                              const std::vector<std::string>& values) {
    CounterMap counters;
    const std::size_t count = header.size() < values.size()
                                  ? header.size()
                                  : values.size();

    for (std::size_t index = 1; index < count; ++index) {
        counters[header[index]] = std::stoull(values[index]);
    }

    return counters;
}

std::uint64_t get_counter(const CounterMap& counters, const std::string& name) {
    const auto iterator = counters.find(name);
    if (iterator == counters.end()) {
        return 0;
    }
    return iterator->second;
}

void read_snmp(TcpSample& sample) {
    std::ifstream proc_net_snmp("/proc/net/snmp");
    if (!proc_net_snmp.is_open()) {
        throw std::runtime_error("failed to open /proc/net/snmp");
    }

    std::string header_line;
    std::string values_line;
    while (std::getline(proc_net_snmp, header_line) &&
           std::getline(proc_net_snmp, values_line)) {
        const std::vector<std::string> header = split_fields(header_line);
        const std::vector<std::string> values = split_fields(values_line);
        if (header.empty() || values.empty() || header[0] != "Tcp:" ||
            values[0] != "Tcp:") {
            continue;
        }

        const CounterMap counters = parse_counter_rows(header, values);
        sample.in_segments = get_counter(counters, "InSegs");
        sample.out_segments = get_counter(counters, "OutSegs");
        sample.retransmitted_segments = get_counter(counters, "RetransSegs");
        sample.resets_sent = get_counter(counters, "OutRsts");
        return;
    }

    throw std::runtime_error("failed to parse Tcp counters from /proc/net/snmp");
}

void read_netstat(TcpSample& sample) {
    std::ifstream proc_net_netstat("/proc/net/netstat");
    if (!proc_net_netstat.is_open()) {
        return;
    }

    std::string header_line;
    std::string values_line;
    while (std::getline(proc_net_netstat, header_line) &&
           std::getline(proc_net_netstat, values_line)) {
        const std::vector<std::string> header = split_fields(header_line);
        const std::vector<std::string> values = split_fields(values_line);
        if (header.empty() || values.empty() || header[0] != "TcpExt:" ||
            values[0] != "TcpExt:") {
            continue;
        }

        const CounterMap counters = parse_counter_rows(header, values);
        sample.listen_overflows = get_counter(counters, "ListenOverflows");
        sample.listen_drops = get_counter(counters, "ListenDrops");
        sample.timeouts = get_counter(counters, "TCPTimeouts");
        return;
    }
}

}  // namespace

TcpSample TcpCollector::read_sample() const {
    TcpSample sample;
    read_snmp(sample);
    read_netstat(sample);
    return sample;
}
