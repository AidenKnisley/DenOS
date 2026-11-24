#include "pic.h"
#include "io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

static void io_wait(void) {
    outb(0x80, 0);
}

void pic_init(void) {
    outb(PIC1_COMMAND, 0x11); io_wait();
    outb(PIC2_COMMAND, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    /* The PIT IRQ is the only interrupt handler installed at present. Keep
     * all other IRQ lines masked until their handlers are registered. */
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(u8 irq) {
    if (irq >= 16) return;
    if (irq >= 8) outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}

u16 pic_get_mask(void) {
    return (u16)inb(PIC1_DATA) | ((u16)inb(PIC2_DATA) << 8);
}

void pic_set_mask(u8 irq) {
    if (irq >= 16) return;
    if (irq < 8) outb(PIC1_DATA, (u8)(inb(PIC1_DATA) | (1u << irq)));
    else outb(PIC2_DATA, (u8)(inb(PIC2_DATA) | (1u << (irq - 8))));
}

void pic_clear_mask(u8 irq) {
    if (irq >= 16) return;
    if (irq < 8) outb(PIC1_DATA, (u8)(inb(PIC1_DATA) & ~(1u << irq)));
    else {
        outb(PIC2_DATA, (u8)(inb(PIC2_DATA) & ~(1u << (irq - 8))));
        outb(PIC1_DATA, (u8)(inb(PIC1_DATA) & ~(1u << 2)));
    }
}
