#ifndef DENOS_NET_INTERNAL_H
#define DENOS_NET_INTERNAL_H
#include "types.h"

#define NET_UDP_PAYLOAD_MAX 1480u

int net_ipv4_send_from(const u8 source[4], const u8 destination[4],
                       u8 protocol, const void *payload, u16 length);
int net_udp_send_from(const u8 source[4], const u8 destination[4],
                      u16 source_port, u16 destination_port,
                      const void *payload, u16 length);
int net_udp_send(const u8 destination[4], u16 source_port,
                 u16 destination_port, const void *payload, u16 length);
int net_udp_receive(u16 local_port, u8 source[4], u16 *source_port,
                    void *payload, u16 capacity, u16 *received);
void net_udp_input(const u8 source[4], const u8 destination[4],
                   const u8 *payload, u16 length);
void net_udp_init(void);
void net_tcp_input(const u8 source[4], const u8 destination[4],
                   const u8 *payload, u16 length);
void net_tcp_init(void);
void net_dns_reset(void);
void net_configure_ipv4(const u8 address[4], const u8 mask[4],
                        const u8 gateway[4], const u8 dns[4], int dhcp);
void net_get_ipv4_config(u8 address[4], u8 mask[4],
                         u8 gateway[4], u8 dns[4]);
int net_is_ready(void);

#endif
