#ifndef DENOS_CPU_H
#define DENOS_CPU_H

#include "types.h"

void cpu_pause(void);
void cpu_interrupt_disable(void);
void cpu_interrupt_enable(void);
u64 cpu_read_flags(void);
void cpu_get_vendor(char *buffer, usize capacity);
/* Cryptographically strong bytes from the x86 RDRAND instruction. Returns 0
 * only when the CPU advertises RDRAND and every requested word succeeds. */
int cpu_random_bytes(void *buffer, usize length);
int cpu_has_rdrand(void);
void cpu_halt(void) __attribute__((noreturn));
void cpu_reboot(void) __attribute__((noreturn));

#endif
