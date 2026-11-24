#include "timer.h"
#include "io.h"
#include "pic.h"
#include "cpu.h"

struct idt_gate {
    u16 low, selector;
    u8 ist, flags;
    u16 middle;
    u32 high, zero;
} __attribute__((packed));
_Static_assert(sizeof(struct idt_gate) == 16u, "x86-64 IDT gate must be 16 bytes");
struct idt_ptr { u16 limit; u64 base; } __attribute__((packed));

struct tss64 {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    u64 reserved2;
    u16 reserved3;
    u16 io_map_base;
} __attribute__((packed));
_Static_assert(sizeof(struct tss64) == 104u, "x86-64 TSS must be 104 bytes");

static struct idt_gate idt[256] __attribute__((aligned(16)));
static volatile u64 ticks;
static int idt_initialized;
extern void irq0_stub(void);
extern void (*exception_stub_table[32])(void);
extern char stack_top[];
extern char double_fault_stack_top[];
extern char interrupt_stack_top[];
extern struct tss64 tss64;
extern u64 gdt_tss_descriptor[2];

static void set_gate(u8 vector, void (*fn)(void), u8 ist) {
    u64 address = (u64)fn;
    idt[vector].low = (u16)address;
    idt[vector].selector = 0x08;
    idt[vector].ist = (u8)(ist & 7u);
    idt[vector].flags = 0x8E;
    idt[vector].middle = (u16)(address >> 16);
    idt[vector].high = (u32)(address >> 32);
    idt[vector].zero = 0;
}

static void load_idt(void) {
    struct idt_ptr pointer = { (u16)(sizeof(idt) - 1u), (u64)idt };
    __asm__ volatile("lidt %0" : : "m"(pointer) : "memory");
}

static void install_tss(void) {
    u64 base = (u64)(usize)&tss64;
    u32 limit = (u32)sizeof(tss64) - 1u;
    u64 low = 0;

    tss64.reserved0 = 0;
    tss64.rsp0 = (u64)(usize)stack_top;
    tss64.rsp1 = 0;
    tss64.rsp2 = 0;
    tss64.reserved1 = 0;
    tss64.ist1 = (u64)(usize)double_fault_stack_top;
    tss64.ist2 = (u64)(usize)interrupt_stack_top;
    tss64.ist3 = 0;
    tss64.ist4 = 0;
    tss64.ist5 = 0;
    tss64.ist6 = 0;
    tss64.ist7 = 0;
    tss64.reserved2 = 0;
    tss64.reserved3 = 0;
    tss64.io_map_base = (u16)sizeof(tss64);

    low |= (u64)(limit & 0xFFFFu);
    low |= (base & 0xFFFFFFu) << 16;
    low |= (u64)0x89u << 40; /* Present, DPL0, available 64-bit TSS. */
    low |= (u64)((limit >> 16) & 0xFu) << 48;
    low |= ((base >> 24) & 0xFFu) << 56;
    gdt_tss_descriptor[0] = low;
    gdt_tss_descriptor[1] = base >> 32;
    __asm__ volatile("ltr %0" : : "rm"((u16)0x18) : "memory");
}

/* Install CPU exception gates before heap, storage, or other subsystems run.
 * IF remains disabled until timer_init() has installed the PIT and PIC state. */
void interrupt_init(void) {
    cpu_interrupt_disable();
    for (u32 i = 0; i < 256u; ++i) {
        idt[i].low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].flags = 0;
        idt[i].middle = 0;
        idt[i].high = 0;
        idt[i].zero = 0;
    }
    for (u32 i = 0; i < 32u; ++i)
        set_gate((u8)i, exception_stub_table[i], 0);
    /* Load working exception gates before LTR. If a malformed TSS descriptor
     * causes #GP, that failure is now diagnosable instead of triple-faulting. */
    load_idt();
    install_tss();
    set_gate(8, exception_stub_table[8], 1); /* Emergency double-fault IST. */
    load_idt();
    idt_initialized = 1;
}

void timer_init(u32 hz) {
    u32 divisor;
    if (!idt_initialized) interrupt_init();
    /* Run PIT callbacks on an emergency kernel interrupt stack rather than
       consuming the interrupted task/shell stack. IST2 is independent of
       IST1, which remains reserved for double-fault recovery. */
    set_gate(32, irq0_stub, 2);
    load_idt();
    pic_init();
    if (hz < 19u) hz = 19u;
    if (hz > 1000u) hz = 1000u;
    divisor = 1193182u / hz;
    outb(0x43, 0x36);
    outb(0x40, (u8)(divisor & 0xFFu));
    outb(0x40, (u8)(divisor >> 8));
    cpu_interrupt_enable();
}

void timer_irq_handler(void) {
    ++ticks;
    pic_send_eoi(0);
}

u64 timer_ticks(void) { return ticks; }
