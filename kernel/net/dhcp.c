#include "net.h"
#include "net_internal.h"
#include "e1000.h"
#include "kstring.h"
#include "timer.h"
#include "entropy.h"

#define DHCP_CLIENT_PORT 68u
#define DHCP_SERVER_PORT 67u
#define DHCP_PACKET_SIZE 548u
#define DHCP_COOKIE 0x63825363u

typedef struct dhcp_options {
    u8 message_type;
    u8 server[4];
    u8 mask[4];
    u8 router[4];
    u8 dns[4];
    u32 lease;
    int has_server, has_mask, has_router, has_dns;
} dhcp_options_t;

static u32 read_be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static int ip_is_zero(const u8 ip[4]) { return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0; }
static int valid_netmask(const u8 mask[4]) {
    int saw_zero = 0;
    for (u32 i = 0; i < 4; ++i) {
        for (u32 bit = 0; bit < 8; ++bit) {
            int set = (mask[i] & (u8)(0x80u >> bit)) != 0;
            if (!set) saw_zero = 1;
            else if (saw_zero) return 0;
        }
    }
    return !ip_is_zero(mask);
}
static void write_be16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void write_be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static int append_option(u8 *packet, u16 capacity, u16 *offset,
                         u8 code, const void *value, u8 length) {
    if (*offset + 2u + length > capacity) return -1;
    packet[(*offset)++] = code;
    packet[(*offset)++] = length;
    if (length) kmemcpy(packet + *offset, value, length);
    *offset = (u16)(*offset + length);
    return 0;
}

static u16 build_dhcp_packet(u8 *packet, u32 xid, u8 message_type,
                             const u8 *requested, const u8 *server) {
    u8 mac[6];
    u16 offset = 240;
    static const u8 parameter_request[] = {1, 3, 6, 15, 51, 54};
    kmemset(packet, 0, DHCP_PACKET_SIZE);
    packet[0] = 1; packet[1] = 1; packet[2] = 6;
    write_be32(packet + 4, xid);
    write_be16(packet + 10, 0x8000u); /* accept broadcast replies */
    kmemcpy(mac, e1000_mac_address(), 6);
    kmemcpy(packet + 28, mac, 6);
    write_be32(packet + 236, DHCP_COOKIE);
    if (append_option(packet, DHCP_PACKET_SIZE, &offset, 53, &message_type, 1) < 0) return 0;
    if (message_type == 3) {
        if (!requested || !server ||
            append_option(packet, DHCP_PACKET_SIZE, &offset, 50, requested, 4) < 0 ||
            append_option(packet, DHCP_PACKET_SIZE, &offset, 54, server, 4) < 0) return 0;
    } else {
        if (append_option(packet, DHCP_PACKET_SIZE, &offset, 55,
                          parameter_request, sizeof(parameter_request)) < 0) return 0;
    }
    {
        u8 client_id[7];
        client_id[0] = 1; kmemcpy(client_id + 1, mac, 6);
        if (append_option(packet, DHCP_PACKET_SIZE, &offset, 61, client_id, sizeof(client_id)) < 0) return 0;
    }
    if (offset >= DHCP_PACKET_SIZE) return 0;
    packet[offset++] = 255;
    return offset;
}

static int parse_dhcp_options(const u8 *packet, u16 length, dhcp_options_t *out) {
    u16 pos = 240;
    if (!packet || !out || length < 241u || read_be32(packet + 236) != DHCP_COOKIE) return -1;
    kmemset(out, 0, sizeof(*out));
    while (pos < length) {
        u8 code = packet[pos++];
        u8 option_length;
        const u8 *value;
        if (code == 0) continue;
        if (code == 255) return 0;
        if (pos >= length) return -1;
        option_length = packet[pos++];
        if (pos + option_length > length) return -1;
        value = packet + pos;
        switch (code) {
            case 53: if (option_length != 1) return -1; out->message_type = value[0]; break;
            case 54:
                if (option_length != 4) return -1;
                kmemcpy(out->server, value, 4); out->has_server = 1;
                break;
            case 1:
                if (option_length != 4) return -1;
                kmemcpy(out->mask, value, 4); out->has_mask = 1;
                break;
            case 3:
                if (option_length < 4 || (option_length % 4u) != 0) return -1;
                kmemcpy(out->router, value, 4); out->has_router = 1;
                break;
            case 6:
                if (option_length < 4 || (option_length % 4u) != 0) return -1;
                kmemcpy(out->dns, value, 4); out->has_dns = 1;
                break;
            case 51: if (option_length != 4) return -1; out->lease = read_be32(value); break;
            default: break;
        }
        pos = (u16)(pos + option_length);
    }
    return -1; /* No end marker: malformed/truncated options. */
}

static int wait_dhcp(u32 xid, u8 expected_type, u32 timeout,
                     u8 address[4], dhcp_options_t *options) {
    u8 packet[DHCP_PACKET_SIZE], source[4];
    u16 source_port, received;
    u64 start = timer_ticks();
    while (timer_ticks() - start < timeout) {
        int got;
        net_poll();
        got = net_udp_receive(DHCP_CLIENT_PORT, source, &source_port,
                              packet, sizeof(packet), &received);
        if (got <= 0 || source_port != DHCP_SERVER_PORT || received < 240u) continue;
        if (packet[0] != 2 || packet[1] != 1 || packet[2] != 6 || read_be32(packet + 4) != xid) continue;
        if (kmemcmp(packet + 28, e1000_mac_address(), 6) != 0) continue;
        if (parse_dhcp_options(packet, received, options) < 0) continue;
        if (options->message_type == 6u) return -2; /* DHCPNAK */
        if (options->message_type != expected_type) continue;
        kmemcpy(address, packet + 16, 4);
        if (!options->has_server) kmemcpy(options->server, source, 4);
        return 0;
    }
    return -1;
}

int net_dhcp_configure(u32 timeout_ticks) {
    static const u8 broadcast[4] = {255, 255, 255, 255};
    static const u8 any[4] = {0, 0, 0, 0};
    u8 packet[DHCP_PACKET_SIZE], offered[4], assigned[4];
    u8 server[4], mask[4] = {255, 255, 255, 0};
    u8 gateway[4] = {0, 0, 0, 0}, dns[4] = {0, 0, 0, 0};
    dhcp_options_t offer, ack;
    u32 xid;
    u16 length;
    if (!net_is_ready()) return -1;
    if (timeout_ticks < 100u) timeout_ticks = 100u;
    xid = entropy_u32() ^ (u32)timer_ticks() ^ 0xD3A05EEDu;
    for (u32 i = 0; i < 4; ++i) xid = (xid << 5) ^ (xid >> 2) ^ e1000_mac_address()[i];
    length = build_dhcp_packet(packet, xid, 1, 0, 0);
    if (!length || net_udp_send_from(any, broadcast, DHCP_CLIENT_PORT,
                                     DHCP_SERVER_PORT, packet, length) < 0) return -2;
    if (wait_dhcp(xid, 2, timeout_ticks / 2u, offered, &offer) < 0) return -3;
    kmemcpy(server, offer.server, 4);
    if (ip_is_zero(server)) return -4;
    length = build_dhcp_packet(packet, xid, 3, offered, server);
    if (!length || net_udp_send_from(any, broadcast, DHCP_CLIENT_PORT,
                                     DHCP_SERVER_PORT, packet, length) < 0) return -4;
    if (wait_dhcp(xid, 5, timeout_ticks - timeout_ticks / 2u, assigned, &ack) < 0) return -5;
    /* wait_dhcp() fills ack.server from the packet source when option 54
     * is absent, so always bind the ACK to the server selected by the OFFER. */
    if (kmemcmp(ack.server, server, 4) != 0) return -5;
    if (kmemcmp(assigned, offered, 4) != 0) return -5;
    if (ack.has_mask) kmemcpy(mask, ack.mask, 4);
    else if (offer.has_mask) kmemcpy(mask, offer.mask, 4);
    if (ack.has_router) kmemcpy(gateway, ack.router, 4);
    else if (offer.has_router) kmemcpy(gateway, offer.router, 4);
    if (ack.has_dns) kmemcpy(dns, ack.dns, 4);
    else if (offer.has_dns) kmemcpy(dns, offer.dns, 4);
    if (dns[0] == 0 && dns[1] == 0 && dns[2] == 0 && dns[3] == 0) kmemcpy(dns, server, 4);
    if (assigned[0] == 0 || assigned[0] >= 224u ||
        (assigned[0] == 255u && assigned[1] == 255u && assigned[2] == 255u && assigned[3] == 255u) ||
        !valid_netmask(mask)) return -6;
    net_configure_ipv4(assigned, mask, gateway, dns, 1);
    return 0;
}
