#ifndef AIDEN_IO_H
#define AIDEN_IO_H
#include "types.h"

/*
 * The kernel uses x86 port I/O. A small mock interface is available to the
 * host-side ATA tests so the disk driver can be exercised without QEMU.
 */
#ifdef DENOS_TEST_IO
void denos_test_outb(u16 port, u8 value);
u8 denos_test_inb(u16 port);
void denos_test_outw(u16 port, u16 value);
u16 denos_test_inw(u16 port);
#define outb denos_test_outb
#define inb  denos_test_inb
#define outw denos_test_outw
#define inw  denos_test_inw
#else
static inline void outb(u16 p, u8 v) {
    __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(p));
}
static inline u8 inb(u16 p) {
    u8 v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void outw(u16 p, u16 v) {
    __asm__ volatile("outw %0,%1" : : "a"(v), "Nd"(p));
}
static inline u16 inw(u16 p) {
    u16 v;
    __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
#endif

#endif
