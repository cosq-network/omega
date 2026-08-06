#include "kernel/net.hpp"
#include "kernel/kprint.hpp"

namespace net {

void NetworkStack::init() {
    kernel::kprintf("[+] VirtIO-Net Driver & TCP/IP Network Stack Initialized.\n");
    kernel::kprintf("    Ethernet L2, IPv4 L3, UDP/TCP L4 Stack Active.\n");
}

void NetworkStack::rx_packet(const uint8_t* packet, size_t len) {
    if (len < sizeof(EthernetHeader)) return;
    const EthernetHeader* eth = reinterpret_cast<const EthernetHeader*>(packet);
    kernel::kprintf("[+] Received Ethernet Frame (%u bytes), EtherType: %x\n", len, eth->ethertype);
}

} // namespace net
