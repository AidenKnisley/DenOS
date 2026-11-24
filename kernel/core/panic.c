#include "panic.h"
#include "cpu.h"
#include "serial.h"

void kernel_panic_at(const char *reason, const char *file, int line) {
    cpu_interrupt_disable();
    serial_write("\033[0m\033[41;37m\n*** DENOS KERNEL PANIC ***\033[0m\n");
    serial_write("Reason: ");
    serial_write(reason ? reason : "unspecified failure");
    serial_write("\nLocation: ");
    serial_write(file ? file : "unknown");
    serial_write(":");
    char digits[16];
    int used = 0;
    unsigned value = line > 0 ? (unsigned)line : 0;
    do {
        digits[used++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && used < (int)sizeof(digits));
    while (used) serial_putc(digits[--used]);
    serial_write("\nThe CPU has halted. A reboot is required.\n");
    for (;;) __asm__ volatile("hlt");
}
