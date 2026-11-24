#ifndef DENOS_E1000_H
#define DENOS_E1000_H
#include "types.h"
int e1000_init(void);
void e1000_poll(void);
int e1000_send(const void *frame, u16 length);
int e1000_link_up(void);
const u8 *e1000_mac_address(void);
u32 e1000_received_frames(void);
u32 e1000_transmitted_frames(void);
void net_receive_frame(const u8 *frame, u16 length);
#endif
