#ifndef DENOS_NET_H
#define DENOS_NET_H
#include "types.h"

typedef struct net_status {
    int initialized;
    int link_up;
    int dhcp_configured;
    u8 mac[6];
    u8 ipv4[4];
    u8 netmask[4];
    u8 gateway[4];
    u8 dns[4];
    u32 rx_frames, tx_frames;
} net_status_t;

int net_init(void);
void net_poll(void);
int net_get_status(net_status_t *status);
int net_ping(const u8 address[4], u32 timeout_ticks);
int net_dhcp_configure(u32 timeout_ticks);
int net_dns_resolve(const char *hostname, u8 address[4], u32 timeout_ticks);
/* One synchronous TCP client is supported; calls are bounded and poll the NIC.
 * net_tcp_receive() returns positive bytes, 0 for peer EOF, or one of these
 * negative transport statuses. */
#define NET_TCP_IO_INVALID (-1)
#define NET_TCP_IO_TIMEOUT (-2)
#define NET_TCP_IO_RESET   (-3)
int net_tcp_connect(const u8 address[4], u16 port, u32 timeout_ticks);
int net_tcp_send(const void *data, u16 length, u32 timeout_ticks);
int net_tcp_receive(void *buffer, u16 capacity, u32 timeout_ticks);
void net_tcp_close(u32 timeout_ticks);
/* TLS transport wrappers. With a TLS-enabled build, certificates and the
 * server name are verified. In the default build these fail closed. */
int net_tls_available(void);
int net_tls_connect(const char *hostname);
int net_tls_send(const void *data, u16 length, u32 timeout_ticks);
int net_tls_receive(void *buffer, u16 capacity, u32 timeout_ticks);
void net_tls_close(u32 timeout_ticks);
int net_http_get(const char *url);

#endif
