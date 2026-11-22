#include "timer.h"
#include "io.h"

struct idt_gate { u16 low, selector; u8 ist, flags; u16 middle; u32 high, zero; } __attribute__((packed));
struct idt_ptr { u16 limit; u64 base; } __attribute__((packed));
static struct idt_gate idt[256] __attribute__((aligned(16)));
static volatile u64 ticks;
extern void irq0_stub(void);

static void set_gate(u8 n, void (*fn)(void)) {
    u64 a = (u64)fn;
    idt[n].low = (u16)a; idt[n].selector = 0x08; idt[n].ist = 0;
    idt[n].flags = 0x8E; idt[n].middle = (u16)(a >> 16);
    idt[n].high = (u32)(a >> 32); idt[n].zero = 0;
}
static void pic_remap(void) {
    outb(0x20,0x11); outb(0xA0,0x11);
    outb(0x21,0x20); outb(0xA1,0x28);
    outb(0x21,0x04); outb(0xA1,0x02);
    outb(0x21,0x01); outb(0xA1,0x01);
    outb(0x21,0xFE); outb(0xA1,0xFF);
}
void timer_init(u32 hz) {
    for (int i=0;i<256;i++) {
        idt[i].low=0; idt[i].selector=0; idt[i].ist=0; idt[i].flags=0;
        idt[i].middle=0; idt[i].high=0; idt[i].zero=0;
    }
    set_gate(32, irq0_stub);
    struct idt_ptr p = { (u16)(sizeof(idt)-1), (u64)idt };
    __asm__ volatile("lidt %0" : : "m"(p));
    pic_remap();
    if (hz < 19) hz = 19;
    if (hz > 1000) hz = 1000;
    u32 divisor = 1193182u / hz;
    outb(0x43,0x36); outb(0x40,(u8)(divisor & 0xFF)); outb(0x40,(u8)(divisor >> 8));
    __asm__ volatile("sti");
}
void timer_irq_handler(void) { ticks++; outb(0x20,0x20); }
u64 timer_ticks(void) { return ticks; }
