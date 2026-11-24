#ifndef DENOS_NET_H
#define DENOS_NET_H
#include "types.h"
typedef struct net_status {
    int initialized;
    int link_up;
    u8 mac[6];
    u8 ipv4[4];
    u8 gateway[4];
    u32 rx_frames, tx_frames;
} net_status_t;
int net_init(void);
void net_poll(void);
int net_get_status(net_status_t *status);
int net_ping(const u8 address[4], u32 timeout_ticks);
#endif
