#ifndef DENOS_CLOCK_H
#define DENOS_CLOCK_H
#include "types.h"
void clock_init(u32 ticks_per_second);
u64 clock_ticks(void);
u64 clock_millis(void);
u64 clock_seconds(void);
u32 clock_frequency(void);
#endif
