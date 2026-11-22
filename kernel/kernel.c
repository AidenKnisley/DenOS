#include "serial.h"
#include "fs.h"
#include "shell.h"
#include "timer.h"
#include "memory.h"
#include "version.h"

/* Keep startup readable on the original 80-column VGA text screen. */
static void boot_status(const char *message) {
    serial_write("\033[1;32m[ OK ]\033[0m ");
    serial_write(message);
    serial_putc('\n');
}

void kernel_main(void) {
    serial_init();
    serial_write("\033[2J\033[H");
    serial_write("\033[1;36m" DENOS_NAME " " DENOS_VERSION "\033[0m\n");
    serial_write("\033[1;34m64-bit operating system\033[0m\n");
    serial_write("\033[1;34m----------------------------------------\033[0m\n");
    serial_write("\n");

    memory_init();
    fs_init();
    timer_init(100);

    boot_status("bootstrap heap initialized");
    if (fs_is_persistent()) {
        boot_status("persistent ATA disk mounted");
    } else {
        serial_write("\033[1;33m[WARN]\033[0m persistent storage unavailable: ");
        serial_write(fs_storage_name());
        serial_write("\n");
    }
    boot_status("Aiden C compiler and AEX virtual machine ready");
    boot_status("IDT, PIC and 100 Hz system timer initialized");

    serial_write("\nFiles are saved as you edit them. With the ATA disk attached, they survive reboot.\n");
    serial_write("Try:  help       list shell commands\n");
    serial_write("      neofetch   show the system information screen\n");
    serial_write("      vim hello.c edit a source file\n\n");

    shell_run();
}
