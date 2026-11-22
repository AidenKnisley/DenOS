#include "serial.h"
#include "fs.h"
#include "types.h"
#include "timer.h"
#include "memory.h"
#include "cvm.h"
#include "version.h"

void editor_open(const char *name);

static char compiled_image[CVM_IMAGE_CAP];

static int eq(const char *a, const char *b) {
    usize i = 0;

    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static int starts(const char *text, const char *prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

static void puts(const char *text) {
    serial_write(text);
}

static void number(u64 value) {
    char digits[24];
    int count = 0;

    if (!value) {
        serial_putc('0');
        return;
    }

    while (value && count < (int)sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }

    while (count) serial_putc(digits[--count]);
}

static void help(void) {
    puts("\033[1;36mDenOS command reference\033[0m\n");
    puts("  ? / help                 show this list\n");
    puts("  cls / clear, echo        screen and text\n");
    puts("  whoami, hostname, uname, ver\n");
    puts("  pwd, dir / ls            list files\n");
    puts("  type / cat FILE          display a file\n");
    puts("  new / touch FILE         create an empty file\n");
    puts("  write FILE TEXT          save text to a file\n");
    puts("  del / rm FILE            remove a file\n");
    puts("  edit / vi / vim FILE     edit a file\n");
    puts("  cc FILE.c PROGRAM        compile (or use: cc FILE.c -o PROGRAM)\n");
    puts("  run PROGRAM              run an AEX program (also: ./PROGRAM)\n");
    puts("  sys / neofetch, mem / free, uptime, df, diskinfo / disk\n");
    puts("  about, guess, ttt, reboot, halt\n");
}

/* The small games are shell demos; the full versions live in userland/apps. */
static void game_guess(void) {
    char answer[32];

    puts("Guess a number from 1 to 5: ");
    serial_readline(answer, sizeof(answer));
    if (eq(answer, "3")) puts("Correct!\n");
    else puts("Nope, it was 3.\n");
}

static void game_ttt(void) {
    puts("Compile the full game with: cc ttt.c ttt\n");
    puts("Then run it with: run ttt\n");
}

static void skip_spaces(const char **cursor) {
    while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
}

/* Read one whitespace-delimited argument without overrunning its destination. */
static int word(const char **cursor, char *out, usize capacity) {
    usize length = 0;
    int overflow = 0;

    if (!cursor || !out || !capacity) return -1;
    skip_spaces(cursor);
    while (**cursor && **cursor != ' ' && **cursor != '\t') {
        if (length + 1 < capacity) out[length++] = **cursor;
        else overflow = 1;
        (*cursor)++;
    }
    out[length] = 0;
    if (overflow) return -1;
    return length ? 1 : 0;
}

static void command_cc(const char *arguments) {
    char source_name[64];
    char second[64];
    char output_name[64];
    char executable_name[64];
    const char *cursor = arguments;

    int source_status = word(&cursor, source_name, sizeof(source_name));
    int second_status = word(&cursor, second, sizeof(second));
    if (source_status < 0 || second_status < 0) {
        puts("cc: filename is too long (maximum 63 characters)\n");
        return;
    }
    if (!source_status || !second_status) {
        puts("usage: cc source.c program  (or: cc source.c -o program)\n");
        return;
    }

    if (eq(second, "-o")) {
        int output_status = word(&cursor, output_name, sizeof(output_name));
        if (output_status < 0) {
            puts("cc: output filename is too long\n");
            return;
        }
        if (!output_status) {
            puts("usage: cc source.c -o program\n");
            return;
        }
    } else {
        usize i = 0;
        while (second[i] && i + 1 < sizeof(output_name)) {
            output_name[i] = second[i];
            i++;
        }
        output_name[i] = 0;
    }

    skip_spaces(&cursor);
    if (*cursor) {
        puts("cc: unexpected extra arguments\n");
        return;
    }

    const char *source = fs_read(source_name);
    if (!source) {
        puts("cc: source file not found: ");
        puts(source_name);
        puts("\n");
        return;
    }

    int size = c_compile(source, compiled_image, sizeof(compiled_image));
    if (size < 0) {
        puts("cc: ");
        puts(c_compile_error());
        puts("\n");
        return;
    }

    usize length = 0;
    while (length < sizeof(output_name) && output_name[length]) length++;
    if (length >= sizeof(output_name)) {
        puts("cc: output filename is too long\n");
        return;
    }
    int has_ax = length >= 3 && output_name[length - 3] == '.' &&
                 output_name[length - 2] == 'a' && output_name[length - 1] == 'x';
    if (!has_ax && length > 60) {
        puts("cc: output name must be 60 characters or fewer without .ax\n");
        return;
    }

    for (usize i = 0; i <= length; i++) executable_name[i] = output_name[i];
    if (!has_ax) {
        executable_name[length++] = '.';
        executable_name[length++] = 'a';
        executable_name[length++] = 'x';
        executable_name[length] = 0;
    }

    if (fs_write(executable_name, compiled_image) < 0) {
        puts("cc: cannot save executable (filesystem full or disk write error)\n");
        return;
    }

    puts("compiled ");
    puts(source_name);
    puts(" -> ");
    puts(executable_name);
    puts(" (AEX bytecode, ");
    number((u64)size);
    puts(" bytes)\n");
}

static void run_program(const char *name) {
    if (!name || !name[0]) {
        puts("usage: run program\n");
        return;
    }

    int result = program_run(name);
    if (result >= 0) {
        puts("[program exited with code ");
        number((u64)result);
        puts("]\n");
    }
}

/* A compact, text-only system summary inspired by 1980s DOS utilities. */
static void system_info(void) {
    puts("\033[1;36m      ____                 ____  _____\033[0m\n");
    puts("\033[1;36m     / __ \\ ___  ____    / __ \\/ ___/\033[0m\n");
    puts("\033[1;36m    / / / // _ \\/ __ \\  / / / /\\__ \\\033[0m\n");
    puts("\033[1;36m   / /_/ //  __/ / / / / /_/ /___/ /\033[0m\n");
    puts("\033[1;36m  /_____/ \\___/_/ /_/  \\____//____/\033[0m\n\n");

    puts("  \033[1;37mSYSTEM\033[0m    " DENOS_NAME " " DENOS_VERSION "\n");
    puts("  \033[1;37mHOST\033[0m      " DENOS_HOSTNAME "\n");
    puts("  \033[1;37mKERNEL\033[0m    x86_64 / freestanding C + NASM\n");
    puts("  \033[1;37mSHELL\033[0m     DenOS command shell (root prompt)\n");
    puts("  \033[1;37mSTORAGE\033[0m   ");
    puts(fs_is_persistent() ? "persistent ATA volume" : fs_storage_name());
    puts("\n  \033[1;37mDISK\033[0m      ");
    puts(fs_disk_model());
    puts("\n  \033[1;37mDISK SIZE\033[0m ");
    if (fs_disk_sector_count()) {
        number((u64)fs_disk_size_bytes());
        puts(" bytes (");
        number((u64)fs_disk_sector_count());
        puts(" sectors)\n");
    } else {
        puts("not detected\n");
    }
    puts("  \033[1;37mVOLUME\033[0m    rootfs (usable file-data pool)\n  \033[1;37mCAPACITY\033[0m  ");
    number((u64)fs_storage_capacity());
    puts(" bytes\n  \033[1;37mUSED\033[0m      ");
    number((u64)fs_bytes_used());
    puts(" bytes\n  \033[1;37mRESERVED\033[0m ");
    number((u64)fs_storage_reserved());
    puts(" bytes\n  \033[1;37mFREE\033[0m      ");
    number((u64)fs_storage_free());
    puts(" bytes\n  \033[1;37mFILES\033[0m     ");
    number((u64)fs_file_count());
    puts(" / 48\n  \033[1;37mUPTIME\033[0m    ");
    number(timer_ticks() / 100);
    puts(" seconds\n\n");
}

static void command(const char *line) {
    if (eq(line, "help") || eq(line, "?")) {
        help();
        return;
    }
    if (eq(line, "clear") || eq(line, "cls")) {
        puts("\033[0m\033[2J\033[H");
        return;
    }
    if (eq(line, "whoami")) {
        puts("root\n");
        return;
    }
    if (eq(line, "uname")) {
        puts(DENOS_NAME " " DENOS_HOSTNAME " " DENOS_VERSION " x86_64 custom-kernel\n");
        return;
    }
    if (eq(line, "hostname")) {
        puts(DENOS_HOSTNAME "\n");
        return;
    }
    if (eq(line, "pwd")) {
        puts("/\n");
        return;
    }
    if (eq(line, "ls") || eq(line, "dir")) {
        fs_list();
        return;
    }
    if (eq(line, "neofetch") || eq(line, "sysinfo") || eq(line, "sys")) {
        system_info();
        return;
    }
    if (eq(line, "ver")) {
        puts(DENOS_NAME " version " DENOS_VERSION "\n");
        return;
    }
    if (eq(line, "about")) {
        puts(DENOS_NAME " " DENOS_VERSION " is an independent 64-bit OS with a small C-subset compiler,\n");
        puts("an AEX bytecode VM, a Vim-style editor and a persistent filesystem.\n");
        puts("The root prompt is a label; user accounts and permissions are not\n");
        puts("implemented yet.\n");
        return;
    }
    if (eq(line, "uptime")) {
        puts("Uptime: ");
        number(timer_ticks() / 100);
        puts(" seconds (PIT estimate)\n");
        return;
    }
    if (eq(line, "free") || eq(line, "meminfo") || eq(line, "mem")) {
        puts("Bootstrap heap: ");
        number((u64)memory_used());
        puts(" / ");
        number((u64)memory_capacity());
        puts(" bytes used\n");
        return;
    }
    if (eq(line, "df")) {
        puts("Filesystem  Type       Used(B)  Reserved(B)  Free(B)  Total(B)\n");
        puts("rootfs      ");
        puts(fs_is_persistent() ? "ata-pio     " : "ram-fs      ");
        number((u64)fs_bytes_used());
        puts("     ");
        number((u64)fs_storage_reserved());
        puts("        ");
        number((u64)fs_storage_free());
        puts("    ");
        number((u64)fs_storage_capacity());
        puts("\n");
        if (!fs_is_persistent()) {
            puts("NOTE: ");
            puts(fs_storage_name());
            puts("\n");
        }
        puts("Backing disk: ");
        puts(fs_disk_model());
        puts(" ( ");
        number((u64)fs_disk_size_bytes());
        puts(" bytes )\n");
        return;
    }
    if (eq(line, "diskinfo") || eq(line, "disk")) {
        puts("ATA DEVICE INFORMATION\n");
        puts("Device: ");
        puts(fs_disk_model());
        puts("\nSectors: ");
        number((u64)fs_disk_sector_count());
        puts("\nCapacity: ");
        number((u64)fs_disk_size_bytes());
        puts(" bytes\nFilesystem: ");
        puts(fs_is_persistent() ? "mounted and persistent\n" : "not mounted; RAM fallback active\n");
        if (!fs_is_persistent()) {
            puts("Probe status: ");
            puts(fs_storage_name());
            puts("\n");
        }
        return;
    }
    if (eq(line, "halt")) {
        puts("System halted. You may close QEMU.\n");
        __asm__ volatile("cli");
        for (;;) __asm__ volatile("hlt");
    }
    if (eq(line, "reboot")) {
        puts("Rebooting...\n");
        __asm__ volatile("cli; mov $0xFE,%al; outb %al,$0x64");
        for (;;) __asm__ volatile("hlt");
    }
    if (eq(line, "cc")) {
        puts("usage: cc source.c -o program\n");
        return;
    }
    if (starts(line, "cc ")) {
        command_cc(line + 3);
        return;
    }
    if (starts(line, "./")) {
        run_program(line + 2);
        return;
    }
    if (eq(line, "run")) {
        puts("usage: run program\n");
        return;
    }
    if (starts(line, "run ")) {
        run_program(line + 4);
        return;
    }
    if (eq(line, "echo")) {
        puts("\n");
        return;
    }
    if (starts(line, "echo ")) {
        puts(line + 5);
        puts("\n");
        return;
    }
    if (starts(line, "cat ") || starts(line, "type ")) {
        const char *filename = starts(line, "cat ") ? line + 4 : line + 5;
        const char *contents = fs_read(filename);
        if (contents) puts(contents);
        else puts("cat: file not found\n");
        return;
    }
    if (starts(line, "vim ") || starts(line, "vi ") || starts(line, "edit ")) {
        const char *filename = starts(line, "vim ") ? line + 4 :
                               starts(line, "vi ") ? line + 3 : line + 5;
        editor_open(filename);
        return;
    }
    if (starts(line, "touch ") || starts(line, "new ")) {
        const char *filename = starts(line, "touch ") ? line + 6 : line + 4;
        if (fs_touch(filename) != 0) puts("new: cannot create file\n");
        return;
    }
    if (starts(line, "rm ") || starts(line, "del ")) {
        const char *filename = starts(line, "rm ") ? line + 3 : line + 4;
        if (fs_remove(filename) != 0) puts("del: file not found\n");
        return;
    }
    if (starts(line, "write ")) {
        char filename[64];
        int length = 0;
        const char *text = line + 6;

        while (*text && *text != ' ' && length < (int)sizeof(filename) - 1) {
            filename[length++] = *text++;
        }
        filename[length] = 0;

        if (*text == ' ') {
            text++;
            if (fs_write(filename, text) == 0) puts("written\n");
            else puts("write: failed (name/data too long or filesystem full)\n");
        } else {
            puts("usage: write filename text\n");
        }
        return;
    }
    if (eq(line, "guess")) {
        game_guess();
        return;
    }
    if (eq(line, "ttt")) {
        game_ttt();
        return;
    }

    puts("command not found: ");
    puts(line);
    puts(". Type 'help'.\n");
}

void shell_run(void) {
    char line[256];

    for (;;) {
        /* The # prompt follows the convention used by classic root shells. */
        puts("\033[1;32mroot\033[0m@\033[1;36mDenOs\033[0m:\033[1;34m/\033[0m# ");
        serial_readline(line, sizeof(line));
        if (line[0]) command(line);
    }
}
