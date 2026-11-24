#include "cpu.h"
#include "io.h"
#include "kstring.h"

void cpu_pause(void) {
    __asm__ volatile("pause" ::: "memory");
}

void cpu_interrupt_disable(void) {
    __asm__ volatile("cli" ::: "memory");
}

void cpu_interrupt_enable(void) {
    __asm__ volatile("sti" ::: "memory");
}

u64 cpu_read_flags(void) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return flags;
}

void cpu_get_vendor(char *buffer, usize capacity) {
    if (!buffer || !capacity) return;
    u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
    char vendor[13];
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0), "c"(0));
    (void)eax;
    vendor[0] = (char)ebx; vendor[1] = (char)(ebx >> 8);
    vendor[2] = (char)(ebx >> 16); vendor[3] = (char)(ebx >> 24);
    vendor[4] = (char)edx; vendor[5] = (char)(edx >> 8);
    vendor[6] = (char)(edx >> 16); vendor[7] = (char)(edx >> 24);
    vendor[8] = (char)ecx; vendor[9] = (char)(ecx >> 8);
    vendor[10] = (char)(ecx >> 16); vendor[11] = (char)(ecx >> 24);
    vendor[12] = '\0';
    kstrlcpy(buffer, vendor, capacity);
}

void cpu_halt(void) {
    cpu_interrupt_disable();
    for (;;) __asm__ volatile("hlt");
}

void cpu_reboot(void) {
    cpu_interrupt_disable();
    /* The 8042 keyboard controller reset command is supported by QEMU and
     * many legacy x86 machines. Bound the wait so a missing controller does
     * not trap the kernel in an unbounded polling loop. */
    for (u32 wait = 0; wait < 100000u; wait++) {
        if (!(inb(0x64) & 0x02)) {
            outb(0x64, 0xFE);
            break;
        }
        cpu_pause();
    }
    for (;;) __asm__ volatile("hlt");
}
