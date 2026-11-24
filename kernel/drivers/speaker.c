#include "speaker.h"
#include "io.h"

#define PIT_INPUT_HZ 1193182u

int speaker_play_tone(u32 frequency_hz) {
    if (frequency_hz < 20u || frequency_hz > 20000u) return -1;
    u32 divisor = PIT_INPUT_HZ / frequency_hz;
    if (!divisor || divisor > 65535u) return -1;

    outb(0x43, 0xB6); /* Channel 2, low/high byte, square-wave mode. */
    outb(0x42, (u8)(divisor & 0xFFu));
    outb(0x42, (u8)(divisor >> 8));
    u8 control = inb(0x61);
    outb(0x61, (u8)(control | 0x03u));
    return 0;
}

void speaker_stop(void) {
    u8 control = inb(0x61);
    outb(0x61, (u8)(control & (u8)~0x03u));
}
