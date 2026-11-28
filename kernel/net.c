#include "net.h"
#include "net_internal.h"
#include "e1000.h"
#include "kstring.h"
#include "timer.h"

#define ETH_ARP 0x0806u
#define ETH_IPV4 0x0800u
#define ARP_REQUEST 1u
#define ARP_REPLY 2u
#define IP_ICMP 1u
#define IP_TCP 6u
#define IP_UDP 17u
#define ICMP_ECHO_REPLY 0u
#define ICMP_ECHO_REQUEST 8u
#define NET_IP_MAX 1500u
#define ARP_CACHE_COUNT 8u
#define ARP_TTL_TICKS 3000u
#define NET_DEFAULT_TIMEOUT 100u

static int net_ready;
static int dhcp_active;
static u8 local_ip[4] = {10, 0, 2, 15};
static u8 netmask[4] = {255, 255, 255, 0};
static u8 gateway_ip[4] = {10, 0, 2, 2};
static u8 dns_ip[4] = {10, 0, 2, 3};
static u8 tx_frame[1514] __attribute__((aligned(16)));
static u16 ip_id = 1;
static volatile int ping_reply;
static u8 ping_target[4];
static u16 ping_identifier;
static u16 ping_sequence;

typedef struct arp_entry {
    u8 ip[4];
    u8 mac[6];
    u64 updated;
    int valid;
} arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE_COUNT];

static u16 read_be16(const u8 *p) { return (u16)(((u16)p[0] << 8) | p[1]); }
static void write_be16(u8 *p, u16 value) { p[0] = (u8)(value >> 8); p[1] = (u8)value; }
static u16 checksum(const u8 *data, u32 length) {
    u32 sum = 0;
    while (length > 1u) { sum += ((u16)data[0] << 8) | data[1]; data += 2; length -= 2; }
    if (length) sum += (u16)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)~sum;
}
static int ip_equal(const u8 *a, const u8 *b) { return kmemcmp(a, b, 4) == 0; }
static int ip_zero(const u8 *ip) { return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0; }
static int ip_broadcast(const u8 *ip) { return ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255; }
static int mac_zero(const u8 *mac) {
    for (u32 i = 0; i < 6; ++i) if (mac[i]) return 0;
    return 1;
}
static int ip_same_subnet(const u8 *a, const u8 *b) {
    for (u32 i = 0; i < 4; ++i) if ((a[i] & netmask[i]) != (b[i] & netmask[i])) return 0;
    return 1;
}
static void ethernet_header(u8 *frame, const u8 *destination_mac, u16 type) {
    kmemcpy(frame, destination_mac, 6);
    kmemcpy(frame + 6, e1000_mac_address(), 6);
    write_be16(frame + 12, type);
}
static void arp_remember(const u8 *ip, const u8 *mac) {
    usize slot = ARP_CACHE_COUNT;
    u64 oldest = ~(u64)0;
    u64 now = timer_ticks();
    if (!ip || !mac || ip_zero(ip) || ip_broadcast(ip) || ip[0] >= 224u ||
        mac_zero(mac) || (mac[0] & 1u)) return;
    for (usize i = 0; i < ARP_CACHE_COUNT; ++i) {
        if (arp_cache[i].valid && ip_equal(arp_cache[i].ip, ip)) { slot = i; break; }
        if (!arp_cache[i].valid) { slot = i; break; }
        if (arp_cache[i].updated < oldest) { oldest = arp_cache[i].updated; slot = i; }
    }
    if (slot >= ARP_CACHE_COUNT) return;
    kmemcpy(arp_cache[slot].ip, ip, 4);
    kmemcpy(arp_cache[slot].mac, mac, 6);
    arp_cache[slot].updated = now;
    arp_cache[slot].valid = 1;
}
static int arp_lookup(const u8 *ip, u8 *mac) {
    u64 now = timer_ticks();
    for (usize i = 0; i < ARP_CACHE_COUNT; ++i) {
        if (!arp_cache[i].valid) continue;
        if (now - arp_cache[i].updated > ARP_TTL_TICKS) { arp_cache[i].valid = 0; continue; }
        if (ip_equal(ip, arp_cache[i].ip)) { kmemcpy(mac, arp_cache[i].mac, 6); return 1; }
    }
    return 0;
}
static int send_arp(u16 operation, const u8 *target_mac, const u8 *target_ip) {
    u8 *p = tx_frame + 14;
    static const u8 zero_mac[6] = {0, 0, 0, 0, 0, 0};
    ethernet_header(tx_frame, target_mac, ETH_ARP);
    write_be16(p, 1); write_be16(p + 2, ETH_IPV4); p[4] = 6; p[5] = 4;
    write_be16(p + 6, operation);
    kmemcpy(p + 8, e1000_mac_address(), 6);
    kmemcpy(p + 14, local_ip, 4);
    kmemcpy(p + 18, operation == ARP_REQUEST ? zero_mac : (target_mac ? target_mac : zero_mac), 6);
    kmemcpy(p + 24, target_ip, 4);
    return e1000_send(tx_frame, 42);
}
static void handle_arp(const u8 *p, u16 length, const u8 *ethernet_source) {
    u16 operation;
    if (length < 28u || !ethernet_source || read_be16(p) != 1u ||
        read_be16(p + 2) != ETH_IPV4 || p[4] != 6u || p[5] != 4u) return;
    operation = read_be16(p + 6);
    if ((operation != ARP_REQUEST && operation != ARP_REPLY) ||
        kmemcmp(p + 8, ethernet_source, 6) != 0 || mac_zero(p + 8) || (p[8] & 1u)) return;
    if (!ip_zero(p + 14)) arp_remember(p + 14, p + 8);
    if (operation == ARP_REQUEST && ip_equal(p + 24, local_ip))
        (void)send_arp(ARP_REPLY, p + 8, p + 14);
}
static int arp_resolve(const u8 *ip, u8 *mac, u32 timeout_ticks) {
    static const u8 broadcast[6] = {255, 255, 255, 255, 255, 255};
    u64 start;
    if (arp_lookup(ip, mac)) return 0;
    if (send_arp(ARP_REQUEST, broadcast, ip) < 0) return -1;
    start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (arp_lookup(ip, mac)) return 0;
    }
    return -1;
}

int net_ipv4_send_from(const u8 source[4], const u8 destination[4],
                       u8 protocol, const void *payload, u16 length) {
    u8 destination_mac[6];
    const u8 *next_hop;
    u8 *ip;
    u16 total;
    static const u8 broadcast_mac[6] = {255, 255, 255, 255, 255, 255};
    if (!net_ready || !source || !destination || (!payload && length) || length > NET_IP_MAX - 20u ||
        (destination[0] >= 224u && !ip_broadcast(destination))) return -1;
    total = (u16)(20u + length);
    if (ip_broadcast(destination)) {
        next_hop = 0;
        kmemcpy(destination_mac, broadcast_mac, 6);
    } else {
        next_hop = ip_same_subnet(destination, source) ? destination : gateway_ip;
        if (ip_zero(next_hop) || arp_resolve(next_hop, destination_mac, NET_DEFAULT_TIMEOUT) < 0) return -2;
    }
    (void)next_hop;
    ethernet_header(tx_frame, destination_mac, ETH_IPV4);
    ip = tx_frame + 14;
    kmemset(ip, 0, 20);
    ip[0] = 0x45;
    write_be16(ip + 2, total);
    write_be16(ip + 4, ip_id++);
    write_be16(ip + 6, 0x4000u);
    ip[8] = 64;
    ip[9] = protocol;
    kmemcpy(ip + 12, source, 4);
    kmemcpy(ip + 16, destination, 4);
    write_be16(ip + 10, checksum(ip, 20));
    if (length) kmemcpy(ip + 20, payload, length);
    return e1000_send(tx_frame, (u16)(14u + total));
}
int net_is_ready(void) { return net_ready; }
void net_get_ipv4_config(u8 address[4], u8 mask[4], u8 gateway[4], u8 dns[4]) {
    if (address) kmemcpy(address, local_ip, 4);
    if (mask) kmemcpy(mask, netmask, 4);
    if (gateway) kmemcpy(gateway, gateway_ip, 4);
    if (dns) kmemcpy(dns, dns_ip, 4);
}
void net_configure_ipv4(const u8 address[4], const u8 mask[4],
                        const u8 gateway[4], const u8 dns[4], int dhcp) {
    if (!address || !mask || !gateway || !dns || ip_zero(address)) return;
    kmemcpy(local_ip, address, 4);
    kmemcpy(netmask, mask, 4);
    kmemcpy(gateway_ip, gateway, 4);
    kmemcpy(dns_ip, dns, 4);
    dhcp_active = dhcp ? 1 : 0;
    kmemset(arp_cache, 0, sizeof(arp_cache));
    net_udp_init();
    net_dns_reset();
}
static void handle_ipv4(const u8 *p, u16 length, const u8 *source_mac) {
    u8 ihl, protocol;
    u16 total, fragment;
    const u8 *src, *dst, *icmp;
    u16 icmp_len;
    if (length < 20u || (p[0] >> 4) != 4u || p[8] == 0u) return;
    ihl = (u8)((p[0] & 15u) * 4u);
    total = read_be16(p + 2);
    if (ihl < 20u || total < ihl || total > length || checksum(p, ihl) != 0) return;
    fragment = read_be16(p + 6);
    /* Reject the reserved flag and every fragmented datagram. DF is fine. */
    if (fragment & 0xBFFFu) return;
    src = p + 12; dst = p + 16; protocol = p[9];
    if (!ip_equal(dst, local_ip) && !ip_broadcast(dst)) {
        /* DHCP replies may be unicast to yiaddr before it becomes our address. */
        if (protocol != IP_UDP || total < (u16)(ihl + 8u) ||
            read_be16(p + ihl + 2u) != 68u) return;
    }
    if (source_mac && !ip_zero(src)) arp_remember(src, source_mac);
    if (protocol == IP_UDP) {
        net_udp_input(src, dst, p + ihl, (u16)(total - ihl));
        return;
    }
    if (protocol == IP_TCP) {
        if (ip_equal(dst, local_ip)) net_tcp_input(src, dst, p + ihl, (u16)(total - ihl));
        return;
    }
    if (protocol != IP_ICMP || !ip_equal(dst, local_ip) || total < (u16)(ihl + 8u)) return;
    icmp = p + ihl;
    icmp_len = (u16)(total - ihl);
    if (checksum(icmp, icmp_len) != 0) return;
    if (icmp[0] == ICMP_ECHO_REQUEST) {
        u8 *out = tx_frame + 14;
        u16 ip_len = (u16)(20u + icmp_len);
        u8 peer_mac[6];
        if (!arp_lookup(src, peer_mac)) return;
        ethernet_header(tx_frame, peer_mac, ETH_IPV4);
        kmemset(out, 0, 20);
        out[0] = 0x45; write_be16(out + 2, ip_len); write_be16(out + 4, ip_id++);
        write_be16(out + 6, 0x4000u); out[8] = 64; out[9] = IP_ICMP;
        kmemcpy(out + 12, local_ip, 4); kmemcpy(out + 16, src, 4);
        write_be16(out + 10, checksum(out, 20));
        kmemcpy(out + 20, icmp, icmp_len);
        out[20] = ICMP_ECHO_REPLY; out[22] = out[23] = 0;
        write_be16(out + 22, checksum(out + 20, icmp_len));
        (void)e1000_send(tx_frame, (u16)(14u + ip_len));
    } else if (icmp[0] == ICMP_ECHO_REPLY && ping_reply == 0 &&
               ip_equal(src, ping_target) && read_be16(icmp + 4) == ping_identifier &&
               read_be16(icmp + 6) == ping_sequence) {
        ping_reply = 1;
    }
}
void net_receive_frame(const u8 *frame, u16 length) {
    u16 type;
    if (!frame || length < 14u) return;
    type = read_be16(frame + 12);
    if (type == ETH_ARP) handle_arp(frame + 14, (u16)(length - 14u), frame + 6);
    else if (type == ETH_IPV4) handle_ipv4(frame + 14, (u16)(length - 14u), frame + 6);
}
int net_init(void) {
    if (net_ready) return 0;
    if (e1000_init() < 0) return -1;
    kmemset(arp_cache, 0, sizeof(arp_cache));
    dhcp_active = 0;
    net_udp_init();
    net_tcp_init();
    net_dns_reset();
    net_ready = 1;
    return 0;
}
void net_poll(void) { if (net_ready) e1000_poll(); }
int net_get_status(net_status_t *status) {
    if (!status) return -1;
    kmemset(status, 0, sizeof(*status));
    status->initialized = net_ready;
    if (!net_ready) return -1;
    status->link_up = e1000_link_up();
    status->dhcp_configured = dhcp_active;
    kmemcpy(status->mac, e1000_mac_address(), 6);
    kmemcpy(status->ipv4, local_ip, 4);
    kmemcpy(status->netmask, netmask, 4);
    kmemcpy(status->gateway, gateway_ip, 4);
    kmemcpy(status->dns, dns_ip, 4);
    status->rx_frames = e1000_received_frames();
    status->tx_frames = e1000_transmitted_frames();
    return 0;
}
int net_ping(const u8 address[4], u32 timeout_ticks) {
    u8 icmp_packet[8];
    u16 ip_len = 28;
    u64 start;
    if (!net_ready || !address || ip_zero(address) || ip_broadcast(address) || address[0] >= 224u) return -1;
    kmemcpy(ping_target, address, 4);
    ping_identifier = (u16)(timer_ticks() ^ 0xD309u);
    ping_sequence++;
    ping_reply = 0;
    (void)ip_len;
    kmemset(icmp_packet, 0, sizeof(icmp_packet));
    icmp_packet[0] = ICMP_ECHO_REQUEST;
    write_be16(icmp_packet + 4, ping_identifier);
    write_be16(icmp_packet + 6, ping_sequence);
    write_be16(icmp_packet + 2, checksum(icmp_packet, sizeof(icmp_packet)));
    if (net_ipv4_send_from(local_ip, address, IP_ICMP, icmp_packet, sizeof(icmp_packet)) < 0) return -2;
    start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (ping_reply) return 0;
    }
    return -4;
}
