#ifndef TCP_COLLECTOR_H
#define TCP_COLLECTOR_H

#include <cstdint>

struct TcpSample {
    std::uint64_t in_segments = 0;
    std::uint64_t out_segments = 0;
    std::uint64_t retransmitted_segments = 0;
    std::uint64_t resets_sent = 0;
    std::uint64_t listen_overflows = 0;
    std::uint64_t listen_drops = 0;
    std::uint64_t timeouts = 0;
};

class TcpCollector {
public:
    TcpSample read_sample() const;
};

#endif
