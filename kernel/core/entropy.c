#include "entropy.h"
#include "timer.h"
static u64 state;
static int seeded;
static u64 read_tsc(void) {
    u32 low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}
void entropy_init(void) {
    state = read_tsc() ^ (timer_ticks() * 0x9E3779B97F4A7C15ull) ^
            (u64)(usize)&state ^ 0xD1B54A32D192ED03ull;
    if (!state) state = 0xA0761D6478BD642Full;
    seeded = 1;
}
u64 entropy_next(void) {
    if (!seeded) entropy_init();
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 0x2545F4914F6CDD1Dull;
}
u32 entropy_u32(void) { return (u32)(entropy_next() >> 32); }
int entropy_is_seeded(void) { return seeded; }
