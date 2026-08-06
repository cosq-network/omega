#ifndef OMEGA_NET_STACK_HPP
#define OMEGA_NET_STACK_HPP

#include "std/cstdint.hpp"

namespace net {

struct EthernetHeader {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
} __attribute__((packed));

struct IPv4Header {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed));

class NetworkStack {
public:
    static void init();
    static void rx_packet(const uint8_t* packet, size_t len);
};

} // namespace net

#endif // OMEGA_NET_STACK_HPP
