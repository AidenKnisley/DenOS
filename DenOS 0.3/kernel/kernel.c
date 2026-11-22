#include "serial.h"
#include "fs.h"
#include "shell.h"
#include "timer.h"
#include "memory.h"

void kernel_main(void) {
    serial_init();
    serial_write("\033[2J\033[H\033[1;32mDenOS 0.3\033[0m\n"
                 "64-bit custom kernel | VGA + PS/2 shell\n");
    memory_init();
    fs_init();
    timer_init(100);
    serial_write("[ OK ] bootstrap heap initialized\n");
    serial_write("[ OK ] filesystem mounted: ");
    serial_write(fs_storage_name());
    serial_write("\n[ OK ] Aiden C subset compiler + AEX bytecode VM ready\n"
                 "[ OK ] IDT/PIC/PIT timer initialized\n\n"
                 "Files are saved as you write them and survive reboot when an ATA disk is attached.\n"
                 "Compile a program: cc hello.c -o hello\n"
                 "Run a program:     ./hello\n"
                 "Type 'help' for commands.\n\n");
    shell_run();
}
