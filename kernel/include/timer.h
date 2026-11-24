#ifndef AIDEN_TIMER_H
#define AIDEN_TIMER_H
#include "types.h"
void interrupt_init(void);
void timer_init(u32 hz);
u64 timer_ticks(void);
void timer_irq_handler(void);
#endif
