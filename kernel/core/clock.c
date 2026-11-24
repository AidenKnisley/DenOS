#include "clock.h"
#include "timer.h"
static u32 frequency = 100u;
void clock_init(u32 hz) { if (hz) frequency = hz; }
u64 clock_ticks(void) { return timer_ticks(); }
u64 clock_millis(void) {
    u64 ticks = timer_ticks();
    return (ticks / frequency) * 1000u + ((ticks % frequency) * 1000u) / frequency;
}
u64 clock_seconds(void) { return timer_ticks() / frequency; }
u32 clock_frequency(void) { return frequency; }
