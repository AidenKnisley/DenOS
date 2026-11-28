#include "net_internal.h"
#include "net.h"
#include "kstring.h"

#define UDP_QUEUE_SLOTS 4u
#define UDP_HEADER_SIZE 8u

typedef struct udp_packet {
    u8 source[4];
    u16 source_port;
    u16 destination_port;
    u16 length;
    u8 data[NET_UDP_PAYLOAD_MAX];
    int used;
} udp_packet_t;
static udp_packet_t queue[UDP_QUEUE_SLOTS];
static u32 queue_write;
static u32 dropped_packets;

static u16 get_be16(const u8 *p) { return (u16)(((u16)p[0] << 8) | p[1]); }
static void put_be16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static u32 checksum_add(u32 sum, const u8 *p, u32 n) {
    while (n > 1u) { sum += ((u16)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) sum += (u16)p[0] << 8;
    return sum;
}
static u16 checksum_finish(u32 sum) {
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)~sum;
}
static u16 udp_checksum(const u8 source[4], const u8 destination[4],
                        const u8 *udp, u16 length) {
    u8 pseudo[4];
    u32 sum = 0;
    sum = checksum_add(sum, source, 4);
    sum = checksum_add(sum, destination, 4);
    pseudo[0] = 0; pseudo[1] = 17;
    put_be16(pseudo + 2, length);
    sum = checksum_add(sum, pseudo, 4);
    sum = checksum_add(sum, udp, length);
    return checksum_finish(sum);
}

void net_udp_init(void) {
    kmemset(queue, 0, sizeof(queue));
    queue_write = 0;
    dropped_packets = 0;
}

int net_udp_send_from(const u8 source[4], const u8 destination[4],
                      u16 source_port, u16 destination_port,
                      const void *payload, u16 length) {
    u8 packet[NET_UDP_PAYLOAD_MAX];
    u16 udp_length;
    u16 check;
    if (!source || !destination || !source_port || !destination_port ||
        (!payload && length) || length > NET_UDP_PAYLOAD_MAX - UDP_HEADER_SIZE)
        return -1;
    udp_length = (u16)(UDP_HEADER_SIZE + length);
    put_be16(packet, source_port);
    put_be16(packet + 2, destination_port);
    put_be16(packet + 4, udp_length);
    packet[6] = packet[7] = 0;
    if (length) kmemcpy(packet + UDP_HEADER_SIZE, payload, length);
    check = udp_checksum(source, destination, packet, udp_length);
    if (!check) check = 0xFFFFu;
    put_be16(packet + 6, check);
    return net_ipv4_send_from(source, destination, 17, packet, udp_length);
}

int net_udp_send(const u8 destination[4], u16 source_port,
                 u16 destination_port, const void *payload, u16 length) {
    u8 source[4];
    net_get_ipv4_config(source, 0, 0, 0);
    return net_udp_send_from(source, destination, source_port, destination_port, payload, length);
}

void net_udp_input(const u8 source[4], const u8 destination[4],
                   const u8 *payload, u16 length) {
    u16 source_port, destination_port, udp_length, expected;
    u8 local[4];
    udp_packet_t *slot = 0;
    if (!source || !destination || !payload || length < UDP_HEADER_SIZE) return;
    source_port = get_be16(payload);
    destination_port = get_be16(payload + 2);
    udp_length = get_be16(payload + 4);
    expected = get_be16(payload + 6);
    if (!source_port || !destination_port || udp_length < UDP_HEADER_SIZE || udp_length != length ||
        udp_length > NET_UDP_PAYLOAD_MAX) return;
    net_get_ipv4_config(local, 0, 0, 0);
    if (expected && udp_checksum(source, destination, payload, udp_length) != 0) return;
    /* Only retain datagrams addressed to an IP owned by the interface or the
       limited broadcast address; IPv4 core has already enforced that policy. */
    (void)local;
    for (u32 i = 0; i < UDP_QUEUE_SLOTS; ++i) {
        u32 index = (queue_write + i) % UDP_QUEUE_SLOTS;
        if (!queue[index].used) { slot = &queue[index]; queue_write = (index + 1u) % UDP_QUEUE_SLOTS; break; }
    }
    if (!slot) {
        ++dropped_packets;
        slot = &queue[queue_write]; /* Keep the newest datagram; evict oldest. */
        queue_write = (queue_write + 1u) % UDP_QUEUE_SLOTS;
    }
    slot->used = 1;
    slot->source_port = source_port;
    slot->destination_port = destination_port;
    slot->length = (u16)(udp_length - UDP_HEADER_SIZE);
    kmemcpy(slot->source, source, 4);
    if (slot->length) kmemcpy(slot->data, payload + UDP_HEADER_SIZE, slot->length);
}

int net_udp_receive(u16 local_port, u8 source[4], u16 *source_port,
                    void *payload, u16 capacity, u16 *received) {
    if (!local_port || (!payload && capacity) || !received) return -1;
    for (u32 i = 0; i < UDP_QUEUE_SLOTS; ++i) {
        udp_packet_t *slot = &queue[i];
        if (!slot->used || slot->destination_port != local_port) continue;
        *received = slot->length;
        if (source) kmemcpy(source, slot->source, 4);
        if (source_port) *source_port = slot->source_port;
        if (slot->length > capacity) {
            slot->used = 0;
            return -2;
        }
        if (slot->length) kmemcpy(payload, slot->data, slot->length);
        slot->used = 0;
        return 1;
    }
    *received = 0;
    return 0;
}
