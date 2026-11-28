#include "serial.h"
#include "fs.h"
#include "types.h"
#include "timer.h"
#include "net.h"
#include "memory.h"
#include "version.h"
#include "parse.h"
#include "log.h"
#include "boot_status.h"
#include "device.h"
#include "heap_diag.h"
#include "kformat.h"
#include "checksum.h"
#include "kstring.h"
#include "path.h"
#include "cpu.h"
#include "rtc.h"
#include "speaker.h"
#include "pmm.h"
#include "paging.h"
#include "vmalloc.h"
#include "scheduler.h"
#include "vfs.h"
#include "ipc.h"
#include "event.h"
#include "clock.h"
#include "bootargs.h"
#include "entropy.h"
#include "uuid.h"
#include "sha256.h"
#include "crc32.h"
#include "env.h"
#include "history.h"
#include "service.h"
#include "partition.h"
#include "ata.h"
#include "elf64.h"
#include "cvm.h"
#include "terminal_apps.h"

void editor_open(const char *name);

static char shell_current_directory[FS_PATH_CAPACITY] = "home/user";

static int eq(const char *a, const char *b) {
    usize i = 0;

    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void puts(const char *text) {
    serial_write(text);
}

static void filesystem_error(const char *operation) {
    puts(operation);
    puts(": ");
    puts(fs_last_error_message());
    puts("\n");
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
    puts("\033[1;36mDenOS " DENOS_VERSION " - COMMAND REFERENCE\033[0m\n\n");
    puts("FILES & DIRECTORIES\n");
    puts("  ls [PATH]       List files             cd PATH        Change directory\n");
    puts("  pwd             Show current path      mkdir PATH     Create directory\n");
    puts("  mkdir -p PATH   Create parent folders  rmdir PATH     Remove empty folder\n");
    puts("  touch FILE      Create empty file\n");
    puts("  cat FILE        Print a file           edit FILE      Edit a text file\n");
    puts("  write F TEXT    Write a text file       cp SRC DST     Copy a file\n");
    puts("  mv SRC DST      Move/rename             rm FILE        Delete a file\n\n");
    puts("AIDEN C PROGRAMMING\n");
    puts("  cc FILE.c -o APP   Compile C source into APP.ax\n");
    puts("  run APP            Run an Aiden C bytecode program\n");
    puts("  cinfo              Show compiler features and limits\n");
    puts("  help programming   Read the language and compiler help\n\n");
    puts("SYSTEM & HARDWARE\n");
    puts("  sysinfo  cpuinfo  date  uptime  free  pmm  paging  tasks  devices\n");
    puts("  df  diskinfo  partitions  mounts  bootlog  hash FILE\n");
    puts("  sha256 FILE  crc32 FILE  services  service start|stop NAME\n\n");
    puts("NETWORK & KERNEL SERVICES\n");
    puts("  net  dhcp  dns HOST  ping HOST  web URL  ipc  events  uuid  random\n\n");
    puts("SHELL & UTILITIES\n");
    puts("  history  !!  env  set KEY VALUE  unset KEY  echo TEXT\n");
    puts("  grep PATTERN FILE  wc FILE  head/tail [-n COUNT] FILE  stat FILE\n");
    puts("  hexdump FILE  tree [PATH]  find [PATH] PATTERN  du [PATH]\n");
    puts("  which PROGRAM  Search built-ins and Aiden C programs on PATH\n");
    puts("  apps  browse [PATH]  calc [EXPR]  notes [FILE]  monitor  arcade\n");
    puts("  whoami  hostname  uname  ver  about  clear  beep  reboot  halt\n\n");
    puts("HELP TOPICS\n");
    puts("  help files | programming | graphics | system | network | shell | editor\n");
    puts("  Quote file names containing spaces: edit \"My Notes.txt\"\n");
}

static void help_topic(const char *topic) {
    if (eq(topic, "files")) {
        puts("FILES AND DIRECTORIES\n\n");
        puts("  ls [PATH]       List a directory\n");
        puts("  cd PATH        Change directory; cd .. moves up\n");
        puts("  pwd            Show the current directory\n");
        puts("  mkdir PATH     Create a directory\n");
        puts("  mkdir -p PATH  Create missing parent directories too\n");
        puts("  rmdir PATH     Remove an empty directory\n");
        puts("  touch FILE     Create an empty file\n");
        puts("  cat FILE       Print a text file\n");
        puts("  edit FILE      Open the text editor\n");
        puts("  write FILE TEXT  Replace a file with TEXT\n");
        puts("  cp SRC DEST    Copy a file\n");
        puts("  mv SRC DEST    Move or rename a file\n");
        puts("  rm FILE        Delete a file\n");
        puts("  find [PATH] PATTERN  Search names recursively (up to six levels); supports * and ?\n");
        puts("  du [PATH]           Total text-file sizes under a path\n\n");
        puts("Paths and file names may be up to 255 characters; spaces are allowed.\n");
        puts("Quote names with spaces: edit \"My Notes.txt\"\n");
        return;
    }
    if (eq(topic, "programming") || eq(topic, "c") || eq(topic, "aiden")) {
        puts("AIDEN C - THE DENOS C SUBSET\n\n");
        puts("  cc hello.c -o hello    Compile hello.c to hello.ax\n");
        puts("  run hello              Execute hello.ax in the AEX virtual machine\n");
        puts("  cinfo                  Print the supported subset and runtime limits\n");
        puts("  cc --version           Show compiler and bytecode version\n\n");
        puts("Supported: global/local variables, fixed 1-D arrays, pointers, dereference,\n");
        puts("array decay, pointer arithmetic, functions/prototypes/recursion, block scopes,\n");
        puts("if/else, while, do/while, for, switch/case/default, break and continue.\n");
        puts("Also: enums, scalar typedefs, sizeof(type/object), ++/--, ?: and compound ops.\n");
        puts("Preprocessor: object macros, #undef, #if 0/1, #ifdef/#ifndef, #else/#endif.\n");
        puts("Strings: strlen/strnlen, strcmp/strncmp, strcpy/strncpy, strcat/strncat.\n");
        puts("Search: strchr, strrchr, strstr, strpbrk, strspn, strcspn.\n");
        puts("More: atoi, abs/labs, ASCII ctype, printf integer formats, scanf, rand/srand.\n");
        puts("All scalar types use signed 64-bit VM cells; pointer/index checks are bounded.\n");
        puts("Limits: no structs/unions, floats, function-like macros or real headers.\n");
        puts("Not ISO C; output is AEX bytecode, not native x86-64. Runs in kernel context.\n");
        puts("Only execute trusted programs. See docs/AIDEN-C.md for the full guide.\n");
        return;
    }
    if (eq(topic, "graphics") || eq(topic, "gfx")) {
        puts("GRAPHICS STATUS\n\n");
        puts("DenOS is terminal-only: boot never switches to a framebuffer desktop.\n");
        puts("The low-level renderer remains available for kernel graphics development,\n");
        puts("but it is not initialized by normal boot and has no desktop/window manager.\n");
        puts("Arcade games draw in the text console using colored ASCII characters.\n");
        puts("See docs/GRAPHICS.md for the inactive low-level renderer API.\n");
        return;
    }
    if (eq(topic, "system")) {
        puts("SYSTEM INSPECTION\n\n");
        puts("  sysinfo                 CPU, memory, storage, and clock summary\n");
        puts("  cpuinfo                 CPU vendor and flags\n");
        puts("  date / uptime           Hardware time and monotonic uptime\n");
        puts("  free / pmm / paging     Heap, physical-page and virtual-map statistics\n");
        puts("  tasks / services        Cooperative kernel task and service state\n");
        puts("  service start|stop NAME Manage a registered service\n");
        puts("  devices / bootlog       Hardware registry and boot diagnostics\n");
        puts("  monitor                 Live text-mode heap/page/storage monitor\n");
        puts("  df / diskinfo           Filesystem and disk capacity\n");
        puts("  partitions              Inspect primary MBR partition entries\n");
        puts("  mounts                  List VFS mounts\n");
        puts("  ipc / events            Inspect IPC queues and kernel event FIFO\n");
        return;
    }
    if (eq(topic, "network") || eq(topic, "net")) {
        puts("NETWORKING\n\n");
        puts("  net                     Show adapter, IPv4, mask, gateway, DNS and counters\n");
        puts("  dhcp                    Request an IPv4 lease, gateway and DNS settings\n");
        puts("  dns HOST                Resolve a hostname to IPv4\n");
        puts("  ping HOST               Send ICMP echo to an IPv4 address or DNS hostname\n");
        puts("  web https://HOST/PATH   Read an HTTP or verified HTTPS web page\n");
        puts("                           PgUp/PgDn scroll; L links; 1-9 open; G URL\n");
        puts("                           B/F history; I images; Q exits\n\n");
        puts("The terminal browser formats common HTML blocks/tables, follows\n");
        puts("relative and absolute links, keeps history, and supports simple CSS\n");
        puts("visibility rules plus literal document.write strings. It is not a\n");
        puts("full JavaScript browser. I supports non-interlaced 8-bit PNG, QOI,\n");
        puts("and uncompressed 24/32-bit BMP images.\n");
        puts("HTTPS is included in the normal build (BearSSL TLS 1.2, verified\n");
        puts("certificates, hardware RDRAND and a correct UTC RTC clock). TLS 1.3\n");
        puts("and certificate revocation checks, IPv6, Wi-Fi, concurrent sockets\n");
        puts("and TCP server mode are not implemented. Use 'dhcp' off-QEMU.\n");
        return;
    }
    if (eq(topic, "editor") || eq(topic, "vim")) {
        puts("DENOS TEXT EDITOR\n\n");
        puts("  i/a/A/I/o/O       Enter insert mode\n");
        puts("  h/j/k/l, arrows   Move cursor; w/b move by word\n");
        puts("  dd/yy/p/P         Delete, yank, and paste lines\n");
        puts("  u / Ctrl-R        Undo / redo\n");
        puts("  /text, ?text      Search forward / backward\n");
        puts("  :w :q :q! :wq     Save, quit, discard, save-and-quit\n");
        puts("  :check            Check matching delimiters/comments/quotes\n");
        puts("  C completion      Floating suggestions: Up/Down or Ctrl-N/P, Tab accepts, Esc dismisses\n");
        puts("  :set nocomplete   Disable suggestions; :set complete enables them\n");
        puts("Completion is lexical and source-aware; it is not a full C semantic language server.\n");
        return;
    }
    if (eq(topic, "shell")) {
        puts("SHELL AND UTILITIES\n\n");
        puts("  Up / Down               Browse RAM-only command history\n");
        puts("  !!                      Repeat the most recent command\n");
        puts("  env                     List environment variables\n");
        puts("  set KEY VALUE           Set a shell variable\n");
        puts("  unset KEY               Remove a shell variable\n");
        puts("  $NAME                   Expand an environment variable in a command\n");
        puts("  uuid / random / beep    Small system utilities\n");
        puts("  which PROGRAM          Find a built-in or Aiden C program on PATH\n");
        puts("  reboot / halt           Restart or stop the virtual machine\n");
        return;
    }
    puts("Unknown help topic. Topics: files, programming, graphics, system, network, shell\n");
}

static void skip_spaces(const char **cursor) {
    while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
}

/* Read one shell argument, supporting single/double-quoted names and escaped
 * whitespace so documents such as "Meeting Notes.txt" are addressable. */
static int word(const char **cursor, char *out, usize capacity) {
    usize length = 0;
    char quote = 0;
    int closed = 1;

    if (!cursor || !*cursor || !out || !capacity) return -1;
    skip_spaces(cursor);
    if (**cursor == '\'' || **cursor == '"') {
        quote = *(*cursor)++;
        closed = 0;
    }
    while (**cursor) {
        char value = **cursor;
        if (quote) {
            if (value == quote) { (*cursor)++; closed = 1; break; }
        } else if (value == ' ' || value == '\t') {
            break;
        }
        if (value == '\\' && (*cursor)[1]) {
            (*cursor)++;
            value = **cursor;
        }
        if (length + 1 >= capacity) return -1;
        out[length++] = value;
        (*cursor)++;
    }
    if (!closed) return -1;
    out[length] = 0;
    return length ? 1 : 0;
}

/* A concise system-information page suitable for a modern text terminal. */
static void system_info(void) {
    char vendor[16];
    heap_diagnostics_t heap;
    pmm_stats_t pages;
    rtc_datetime_t now;

    cpu_get_vendor(vendor, sizeof(vendor));
    puts("DenOS system information\n");
    puts("  Operating system : " DENOS_NAME " " DENOS_VERSION "\n");
    puts("  Host name        : "); puts(env_get("HOSTNAME") ? env_get("HOSTNAME") : DENOS_HOSTNAME); puts("\n");
    puts("  Kernel           : x86-64 freestanding kernel\n");
    puts("  Session          : local single-user command shell\n");
    puts("  Processor        : "); puts(vendor); puts("\n");
    puts("  Storage          : "); puts(fs_is_persistent() ? fs_disk_model() : fs_storage_name()); puts("\n");
    puts("  Files            : "); number((u64)fs_file_count()); puts("\n");
    puts("  Storage used     : "); number((u64)fs_bytes_used()); puts(" bytes\n");
    if (memory_diagnostics(&heap) == 0) {
        puts("  Kernel heap      : "); number((u64)heap.used_bytes); puts(" / ");
        number((u64)heap.total_bytes); puts(" bytes\n");
    }
    if (pmm_get_stats(&pages) == 0 && pages.initialized) {
        puts("  Free pages       : "); number(pages.free_pages); puts("\n");
    }
    {
        vmm_stats_t paging;
        if (vmm_get_stats(&paging) == VMM_OK) {
            puts("  Paging           : four-level x86-64");
            if (paging.nx_enabled) puts(" + NX");
            if (paging.write_protect_enabled) puts(" + supervisor WP");
            if (paging.global_pages_enabled) puts(" + PGE");
            puts("\n");
        } else puts("  Paging           : bootstrap map only\n");
    }
    if (rtc_read(&now) == 0) {
        char timestamp[32];
        (void)ksnprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u\n",
                        (unsigned)now.year, (unsigned)now.month, (unsigned)now.day,
                        (unsigned)now.hour, (unsigned)now.minute, (unsigned)now.second);
        puts("  System time      : "); puts(timestamp);
    } else puts("  System time      : unavailable (RTC)\n");
    puts("  Uptime           : "); number(clock_seconds()); puts(" seconds\n");
    puts("\nDenOS is an experimental independent OS, not a Linux distribution.\n");
}

static void command_date(void) {
    rtc_datetime_t now;
    char formatted[48];
    if (rtc_read(&now) < 0) {
        puts("date: RTC did not return a stable, valid clock sample\n");
        return;
    }
    ksnprintf(formatted, sizeof(formatted), "%04u-%02u-%02u %02u:%02u:%02u\n",
              (unsigned)now.year, (unsigned)now.month, (unsigned)now.day,
              (unsigned)now.hour, (unsigned)now.minute, (unsigned)now.second);
    puts(formatted);
}

static void command_devices(void) {
    puts("NAME                    CLASS    STATE     DRIVER\n");
    puts("----------------------------------------------------------\n");
    for (int i = 0; i < device_count(); i++) {
        const device_info_t *item = device_get(i);
        if (!item) continue;
        puts(item->name);
        puts("  ");
        puts(device_class_name((enum device_class)item->device_class));
        puts("  ");
        puts(device_state_name((enum device_state)item->state));
        puts("  ");
        puts(item->driver);
        puts("\n");
    }
    puts("Registered devices: ");
    number((u64)device_count());
    puts("\n");
}

static void command_bootlog(void) {
    puts("BOOT DIAGNOSTICS\n");
    for (int i = 0; i < boot_status_count(); i++) {
        const boot_record_t *record = boot_status_get(i);
        if (!record) continue;
        puts("  [");
        puts(boot_result_name((enum boot_result)record->result));
        puts("] ");
        puts(record->label);
        puts(" (tick ");
        number(record->tick);
        puts(")\n");
    }
    puts("Completed boot records: ");
    number((u64)boot_status_count());
    puts(" / ");
    number(BOOT_STATUS_CAP);
    puts("\n");
}

static void command_cpuinfo(void) {
    char vendor[16];
    cpu_get_vendor(vendor, sizeof(vendor));
    puts("CPU architecture: x86-64\nVendor: ");
    puts(vendor);
    puts("\nRFLAGS: 0x");
    char flags[24];
    ksnprintf(flags, sizeof(flags), "%llx\n", (unsigned long long)cpu_read_flags());
    puts(flags);
}

static void command_hash(const char *filename) {
    const char *contents = fs_read(filename);
    if (!contents) {
        puts("hash: cannot read file (check the name and filesystem)\n");
        return;
    }
    usize length = kstrnlen(contents, FS_FILE_MAX_BYTES);
    char hash[16];
    ksnprintf(hash, sizeof(hash), "%08x", kfnv1a32(contents, length));
    puts("FNV-1a 32: ");
    puts(hash);
    puts("  ");
    puts(filename);
    puts(" ( ");
    number((u64)length);
    puts(" bytes )\n");
}

static void command_beep(void) {
    u64 started;
    if (speaker_play_tone(880) < 0) {
        puts("beep: PC speaker rejected the requested frequency\n");
        return;
    }
    started = timer_ticks();
    while (timer_ticks() - started < 8u) cpu_pause();
    speaker_stop();
}

static int shell_partition_read(void *context, u32 lba, void *buffer) {
    (void)context;
    return ata_read_sector(lba, buffer);
}

static int shell_resolve(const char *argument, char output[FS_PATH_CAPACITY]) {
    return fs_path_normalize(shell_current_directory, argument, output, FS_PATH_CAPACITY);
}

static char aiden_image[CVM_IMAGE_CAP];

static int path_ends_with(const char *path, const char *suffix) {
    usize n = kstrnlen(path, FS_PATH_CAPACITY);
    usize m = kstrnlen(suffix, FS_PATH_CAPACITY);
    if (n >= FS_PATH_CAPACITY || m > n) return 0;
    for (usize i = 0; i < m; ++i)
        if (path[n - m + i] != suffix[i]) return 0;
    return 1;
}

static int ensure_aex_extension(char path[FS_PATH_CAPACITY]) {
    usize n = kstrnlen(path, FS_PATH_CAPACITY);
    if (n >= FS_PATH_CAPACITY) return -1;
    if (path_ends_with(path, ".ax")) return 0;
    if (n + 3u >= FS_PATH_CAPACITY) return -1;
    path[n++] = '.';
    path[n++] = 'a';
    path[n++] = 'x';
    path[n] = '\0';
    return 0;
}

static int default_aex_path(const char *source, char output[FS_PATH_CAPACITY]) {
    usize n = kstrnlen(source, FS_PATH_CAPACITY);
    if (n >= FS_PATH_CAPACITY || kstrlcpy(output, source, FS_PATH_CAPACITY) >= FS_PATH_CAPACITY)
        return -1;
    if (path_ends_with(output, ".c")) {
        if (n + 1u >= FS_PATH_CAPACITY) return -1;
        output[n - 1u] = 'a';
        output[n] = 'x';
        output[n + 1u] = '\0';
        return 0;
    }
    return ensure_aex_extension(output);
}

static int shell_has_path_separator(const char *name) {
    if (!name) return 0;
    for (usize i = 0; name[i]; ++i) if (name[i] == '/') return 1;
    return 0;
}

static int shell_try_program(const char *candidate,
                             char output[FS_PATH_CAPACITY]) {
    if (shell_resolve(candidate, output) < 0 || ensure_aex_extension(output) < 0)
        return -1;
    return fs_is_file(output) ? 0 : -1;
}

/* Resolve an AEX program from an explicit path, the working directory, then PATH. */
static int shell_find_program(const char *name, char output[FS_PATH_CAPACITY]) {
    char candidate[FS_PATH_CAPACITY];
    const char *path_value;
    if (!name || !name[0]) return -1;
    if (shell_has_path_separator(name)) return shell_try_program(name, output);

    if (ksnprintf(candidate, sizeof(candidate), "%s/%s",
                  shell_current_directory, name) >= (int)sizeof(candidate))
        return -1;
    if (shell_try_program(candidate, output) == 0) return 0;

    path_value = env_get("PATH");
    while (path_value && *path_value) {
        char directory[FS_PATH_CAPACITY];
        usize length = 0;
        while (path_value[length] && path_value[length] != ':' &&
               length + 1u < sizeof(directory)) length++;
        if (path_value[length] == ':' || path_value[length] == '\0') {
            for (usize i = 0; i < length; ++i) directory[i] = path_value[i];
            directory[length] = '\0';
            if (!directory[0]) {
                if (ksnprintf(candidate, sizeof(candidate), "%s/%s",
                              shell_current_directory, name) < (int)sizeof(candidate) &&
                    shell_try_program(candidate, output) == 0) return 0;
            } else if (ksnprintf(candidate, sizeof(candidate), "%s/%s", directory, name) <
                       (int)sizeof(candidate) && shell_try_program(candidate, output) == 0) {
                return 0;
            }
        } else return -1; /* Reject an overlong PATH component. */
        path_value += length;
        if (*path_value == ':') ++path_value;
    }
    return -1;
}

static int shell_is_builtin(const char *name) {
    static const char *const builtins[] = {
        "help","ls","dir","cd","pwd","mkdir","rmdir","touch","new",
        "cat","type","edit","vi","vim","write","cp","mv","rm","del",
        "cc","run","cinfo","grep","wc","head","tail","stat","hexdump",
        "tree","find","du","sha256","crc32","hash","df","diskinfo","disk",
        "partitions","mounts","devices","cpuinfo","sysinfo","sys","neofetch",
        "uptime","date","free","meminfo","mem","pmm","paging","vmstat","tasks",
        "ps","services","service","ipc","events","net","ifconfig","ping","dhcp","dns","web","history",
        "env","set","unset","whoami","hostname","uname","ver","about","clear","cls",
        "beep","reboot","halt","poweroff","uuid","random","bootargs","bootlog",
        "echo","apps","browse","calc","notes","monitor","arcade","which","!!"
    };
    for (usize i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i)
        if (eq(name, builtins[i])) return 1;
    return 0;
}

static void command_cc(const char *cursor) {
    char source_name[FS_PATH_CAPACITY];
    char output_name[FS_PATH_CAPACITY];
    char source_path[FS_PATH_CAPACITY];
    char output_path[FS_PATH_CAPACITY];
    char option[16];
    const char *source;
    int status;

    status = word(&cursor, source_name, sizeof(source_name));
    if (status <= 0 || eq(source_name, "--help") || eq(source_name, "-h")) {
        puts("usage: cc SOURCE.c [-o PROGRAM]\n");
        puts("Example: cc hello.c -o hello   then   run hello\n");
        puts("Aiden C: globals, arrays, pointers, functions, switch, enums, typedefs, macros.\n");
        puts("String built-ins: strlen, strcmp, strcpy, strcat, strstr, atoi and ASCII ctype.\n");
        puts("Use cc --version for compiler and bytecode version.\n");
        return;
    }
    if (eq(source_name, "--version") || eq(source_name, "-v")) {
        puts("Aiden C " AIDEN_C_VERSION " for DenOS " DENOS_VERSION " (AEX6 bytecode compiler)\n");
        return;
    }
    if (shell_resolve(source_name, source_path) < 0) {
        puts("cc: source path is invalid or too long\n");
        return;
    }
    source = fs_read(source_path);
    if (!source) {
        puts("cc: cannot read source file: "); puts(source_name); puts("\n");
        return;
    }

    status = word(&cursor, option, sizeof(option));
    if (status < 0) {
        puts("cc: invalid or overlong output argument\n");
        return;
    }
    if (status > 0) {
        if (eq(option, "-o")) {
            if (word(&cursor, output_name, sizeof(output_name)) <= 0) {
                puts("cc: -o requires an output program name\n");
                return;
            }
        } else {
            (void)kstrlcpy(output_name, option, sizeof(output_name));
        }
        if (word(&cursor, option, sizeof(option)) != 0) {
            puts("cc: too many or invalid arguments; use cc SOURCE.c [-o PROGRAM]\n");
            return;
        }
        if (shell_resolve(output_name, output_path) < 0 || ensure_aex_extension(output_path) < 0) {
            puts("cc: output path is invalid or too long\n");
            return;
        }
    } else if (default_aex_path(source_path, output_path) < 0) {
        puts("cc: could not derive output filename; specify -o PROGRAM\n");
        return;
    }

    int image_size = c_compile(source, aiden_image, sizeof(aiden_image));
    if (image_size <= 0) {
        puts("Aiden C: "); puts(c_compile_error()); puts("\n");
        return;
    }
    if (fs_write(output_path, aiden_image) < 0) {
        puts("cc: could not save executable: "); puts(fs_last_error_message()); puts("\n");
        return;
    }
    puts("Aiden C compiled "); puts(source_name); puts(" -> "); puts(output_path);
    puts(" (AEX6, "); number((u64)image_size); puts(" bytes)\n");
}

static void command_run(const char *cursor) {
    char program_name[FS_PATH_CAPACITY];
    char program_path[FS_PATH_CAPACITY];
    char extra[FS_PATH_CAPACITY];
    int result;
    if (word(&cursor, program_name, sizeof(program_name)) <= 0) {
        puts("usage: run PROGRAM\n");
        return;
    }
    if (word(&cursor, extra, sizeof(extra)) != 0) {
        puts("run: Aiden C programs do not accept command-line arguments yet\n");
        return;
    }
    if (shell_find_program(program_name, program_path) < 0) {
        puts("run: program not found: "); puts(program_name);
        puts(" (looked in the current directory and PATH)\n");
        return;
    }
    result = program_run(program_path);
    if (result >= 0) {
        puts("Program exited with status ");
        number((u64)result);
        puts("\n");
    }
}

static void command_cinfo(void) {
    puts("AIDEN C COMPILER\n\n");
    puts("Version         : Aiden C " AIDEN_C_VERSION " (DenOS " DENOS_VERSION ")\n");
    puts("Compiler output : AEX6 bytecode image\n");
    puts("VM compatibility: AEX1 through AEX6\n");
    puts("Execution       : bounded in-kernel AEX virtual machine\n");
    puts("Functions       : prototypes, parameters, recursion and mutual recursion\n");
    puts("Storage         : globals, locals, fixed 1-D arrays and scalar pointers\n");
    puts("Control flow    : if/else, loops, switch/case/default, break, continue\n");
    puts("Types           : integer spellings, enums, scalar typedefs, sizeof(type/object)\n");
    puts("Preprocessor    : object macros, undef, if 0/1, ifdef/ifndef, else/endif\n");
    puts("Expressions     : arithmetic, comparisons, logic, bitwise, shifts, ?:, ++/--\n");
    puts("Built-ins       : stdio subset, string/search helpers, rand/srand, atoi/abs, ASCII ctype\n");
    puts("Formatting      : %d/%i/%u/%x/%X/%o/%p/%c/%s/%% (width ignored)\n");
    puts("Limits          : 32 KiB source, 2048 instructions, 8 KiB data, 96 functions,\n");
    puts("                  32 parameters, 1024 global cells, 8192 local cells, 4096 stack,\n");
    puts("                  128 call frames, 4096 cells per fixed-size array\n\n");
    puts("Not full ISO C: no structs/unions, floats, function pointers, full headers,\n");
    puts("function-like macros, native code, or hosted standard library.\n");
    puts("Types are VM cells, not target ABI widths. Programs share kernel privilege.\n");
}

static void shell_list_directory(const char *directory) {
    usize count = fs_entry_count_in_dir(directory);
    if (!count) { puts("(empty directory)\n"); return; }
    puts("TYPE       SIZE       NAME\n");
    for (usize i = 0; i < count; i++) {
        char name[FS_PATH_CAPACITY];
        char line[FS_PATH_CAPACITY + 64];
        usize size = 0;
        int type = 0;
        if (fs_get_entry_in_dir(directory, i, name, sizeof(name), &size, &type) < 0) continue;
        if (type == FS_ENTRY_DIRECTORY)
            ksnprintf(line, sizeof(line), "directory  --         %s/\n", name);
        else
            ksnprintf(line, sizeof(line), "file       %8lu   %s\n", (unsigned long)size, name);
        puts(line);
    }
}

static void shell_list_vfs(const char *directory) {
    vfs_dirent_t entry;
    usize index = 0;
    int found = 0;
    puts("TYPE       SIZE       NAME\n");
    while (vfs_list(directory, index++, &entry) == 0) {
        char line[FS_PATH_CAPACITY + 64];
        found = 1;
        if (entry.type == VFS_TYPE_DIRECTORY)
            ksnprintf(line, sizeof(line), "directory  --         %s/\n", entry.name);
        else
            ksnprintf(line, sizeof(line), "file       %8lu   %s\n", (unsigned long)entry.size, entry.name);
        puts(line);
    }
    if (!found) puts("(empty directory)\n");
}

static int shell_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void command_wc(const char *path) {
    const char *data = fs_read(path);
    usize bytes = 0, lines = 0, words = 0;
    int in_word = 0;
    if (!data) { puts("wc: cannot read file\n"); return; }
    while (data[bytes]) {
        char c = data[bytes++];
        if (c == '\n') lines++;
        if (shell_is_space(c)) in_word = 0;
        else if (!in_word) { words++; in_word = 1; }
    }
    if (bytes && data[bytes - 1] != '\n') lines++;
    puts("lines  words  bytes  file\n");
    number(lines); puts("      "); number(words); puts("      ");
    number(bytes); puts("  "); puts(path); puts("\n");
}

static void command_grep(const char *pattern, const char *path) {
    const char *data = fs_read(path);
    usize position = 0, line_number = 1;
    int matches = 0;
    usize pattern_length = kstrlen(pattern);
    if (!data || !pattern_length) { puts("usage: grep PATTERN FILE\n"); return; }
    while (data[position]) {
        usize start = position;
        while (data[position] && data[position] != '\n') position++;
        usize length = position - start;
        int found = 0;
        if (pattern_length <= length) {
            for (usize i = 0; i + pattern_length <= length; ++i) {
                if (kmemcmp(data + start + i, pattern, pattern_length) == 0) { found = 1; break; }
            }
        }
        if (found) {
            number(line_number); puts(": ");
            for (usize i = 0; i < length; ++i) serial_putc(data[start + i]);
            puts("\n"); matches++;
        }
        if (data[position] == '\n') position++;
        line_number++;
    }
    if (!matches) puts("grep: no matching lines\n");
    else { number((u64)matches); puts(" matching line(s)\n"); }
}

static void command_lines(const char *path, u32 count, int tail) {
    const char *data = fs_read(path);
    usize length, start = 0;
    u32 lines = 0;
    if (!data) { puts(tail ? "tail: cannot read file\n" : "head: cannot read file\n"); return; }
    length = kstrnlen(data, 65536u);
    if (tail) {
        for (usize i = 0; i < length; ++i) if (data[i] == '\n') lines++;
        if (length && data[length - 1] != '\n') lines++;
        u32 skip = lines > count ? lines - count : 0;
        while (start < length && skip) if (data[start++] == '\n') skip--;
    }
    lines = 0;
    for (usize i = start; i < length; ++i) {
        serial_putc(data[i]);
        if (data[i] == '\n' && ++lines >= count) break;
    }
    if (length > start && data[length - 1] != '\n') puts("\n");
}

static void command_hexdump(const char *path) {
    const char *data = fs_read(path);
    usize length, limit;
    if (!data) { puts("hexdump: cannot read file\n"); return; }
    length = kstrnlen(data, 65536u);
    limit = length < 256u ? length : 256u;
    puts("OFFSET   HEX                                               ASCII\n");
    for (usize row = 0; row < limit; row += 16u) {
        char prefix[24];
        ksnprintf(prefix, sizeof(prefix), "%08lx  ", (unsigned long)row);
        puts(prefix);
        for (usize col = 0; col < 16u; ++col) {
            if (row + col < limit) {
                char byte[4];
                ksnprintf(byte, sizeof(byte), "%02x ", (unsigned char)data[row + col]);
                puts(byte);
            } else puts("   ");
        }
        puts(" ");
        for (usize col = 0; col < 16u && row + col < limit; ++col) {
            unsigned char c = (unsigned char)data[row + col];
            serial_putc(c >= 32u && c < 127u ? (char)c : '.');
        }
        puts("\n");
    }
    if (length > limit) puts("(showing first 256 bytes)\n");
}

static void command_stat(const char *path) {
    vfs_stat_t info;
    char absolute[FS_PATH_CAPACITY + 1u];
    int written;
    if (!path || !path[0]) written = ksnprintf(absolute, sizeof(absolute), "/");
    else written = ksnprintf(absolute, sizeof(absolute), "/%s", path);
    if (written < 0 || (usize)written >= sizeof(absolute) ||
        vfs_stat_path(absolute, &info) < 0) {
        puts("stat: path not found or not visible through VFS\n");
        return;
    }
    puts("Path       : "); puts(absolute); puts("\nType       : ");
    puts(info.type == VFS_TYPE_DIRECTORY ? "directory\n" : "file\n");
    puts("Size       : "); number((u64)info.size); puts(" bytes\n");
    puts("Read-only  : "); puts(info.read_only ? "yes\n" : "no\n");
}

static int shell_join_entry(const char *parent, const char *leaf,
                            char *output, usize capacity);

static void tree_walk(const char *path, int depth) {
    usize count = fs_entry_count_in_dir(path);
    if (depth > 6) { puts("(maximum tree depth reached)\n"); return; }
    for (usize i = 0; i < count; ++i) {
        char name[FS_PATH_CAPACITY], child[FS_PATH_CAPACITY];
        usize size = 0;
        int type = 0;
        if (fs_get_entry_in_dir(path, i, name, sizeof(name), &size, &type) < 0) continue;
        for (int d = 0; d < depth; ++d) puts("  ");
        puts(type == FS_ENTRY_DIRECTORY ? "|- " : "|  ");
        puts(name);
        if (type == FS_ENTRY_DIRECTORY) puts("/");
        puts("\n");
        if (type == FS_ENTRY_DIRECTORY) {
            if (shell_join_entry(path, name, child, sizeof(child)) < 0) {
                for (int d = 0; d <= depth; ++d) puts("  ");
                puts("(path exceeds DenFS limit; subtree skipped)\n");
                continue;
            }
            tree_walk(child, depth + 1);
        }
    }
}

static int shell_join_entry(const char *parent, const char *leaf,
                            char *output, usize capacity) {
    int written;
    if (!parent || !leaf || !output || !capacity) return -1;
    written = parent[0] ? ksnprintf(output, capacity, "%s/%s", parent, leaf)
                        : ksnprintf(output, capacity, "%s", leaf);
    return written < 0 || (usize)written >= capacity ? -1 : 0;
}

static int shell_glob_match(const char *pattern, const char *text) {
    const char *star = 0;
    const char *retry = 0;
    if (!pattern || !text) return 0;
    while (*text) {
        if (*pattern == '?' || *pattern == *text) { ++pattern; ++text; }
        else if (*pattern == '*') { star = pattern++; retry = text; }
        else if (star) { pattern = star + 1; text = ++retry; }
        else return 0;
    }
    while (*pattern == '*') ++pattern;
    return *pattern == '\0';
}

static void find_walk(const char *root, const char *pattern, int depth,
                      u32 *matches) {
    usize count = fs_entry_count_in_dir(root);
    if (depth > 6) return;
    for (usize i = 0; i < count; ++i) {
        char name[FS_PATH_CAPACITY];
        char child[FS_PATH_CAPACITY];
        usize size = 0;
        int type = 0;
        if (fs_get_entry_in_dir(root, i, name, sizeof(name), &size, &type) < 0)
            continue;
        if (shell_glob_match(pattern, name)) {
            if (shell_join_entry(root, name, child, sizeof(child)) == 0) {
                puts("/"); puts(child); puts(type == FS_ENTRY_DIRECTORY ? "/\n" : "\n");
                ++*matches;
            }
        }
        if (type == FS_ENTRY_DIRECTORY && depth < 6) {
            if (shell_join_entry(root, name, child, sizeof(child)) == 0)
                find_walk(child, pattern, depth + 1, matches);
            else {
                puts("find: path too deep to inspect: "); puts(name); puts("\n");
            }
        }
    }
}

static void command_find(const char *cursor) {
    char first[FS_PATH_CAPACITY];
    char second[FS_PATH_CAPACITY];
    char root[FS_PATH_CAPACITY];
    const char *pattern;
    u32 matches = 0;
    int first_status = word(&cursor, first, sizeof(first));
    int second_status;
    int extra_status;
    if (first_status <= 0) { puts("usage: find [PATH] PATTERN\n"); return; }
    second_status = word(&cursor, second, sizeof(second));
    extra_status = word(&cursor, root, sizeof(root));
    if (second_status < 0 || extra_status != 0) {
        puts("find: too many or overlong arguments\n"); return;
    }
    if (second_status > 0) {
        if (shell_resolve(first, root) < 0 || !fs_is_directory(root)) {
            puts("find: PATH must be an existing DenFS directory\n"); return;
        }
        pattern = second;
    } else {
        if (kstrlcpy(root, shell_current_directory, sizeof(root)) >= sizeof(root)) {
            puts("find: current path is too long\n"); return;
        }
        pattern = first;
    }
    find_walk(root, pattern, 0, &matches);
    if (!matches) puts("find: no matching entries\n");
    else { number(matches); puts(" match(es)\n"); }
}

static void du_walk(const char *path, int depth, u64 *bytes, u32 *files) {
    usize count;
    if (depth > 6) return;
    count = fs_entry_count_in_dir(path);
    for (usize i = 0; i < count; ++i) {
        char name[FS_PATH_CAPACITY];
        char child[FS_PATH_CAPACITY];
        usize size = 0;
        int type = 0;
        if (fs_get_entry_in_dir(path, i, name, sizeof(name), &size, &type) < 0)
            continue;
        if (shell_join_entry(path, name, child, sizeof(child)) < 0) continue;
        if (type == FS_ENTRY_DIRECTORY) du_walk(child, depth + 1, bytes, files);
        else { *bytes += (u64)size; ++*files; }
    }
}

static void command_du(const char *cursor) {
    char argument[FS_PATH_CAPACITY];
    char path[FS_PATH_CAPACITY];
    u64 bytes = 0;
    u32 files = 0;
    int status = word(&cursor, argument, sizeof(argument));
    int extra_status = word(&cursor, path, sizeof(path));
    if (status < 0 || extra_status != 0) {
        puts("usage: du [PATH]\n"); return;
    }
    if (status == 0) kstrlcpy(path, shell_current_directory, sizeof(path));
    else if (shell_resolve(argument, path) < 0) {
        puts("du: invalid path\n"); return;
    }
    if (fs_is_file(path)) {
        const char *contents = fs_read(path);
        if (!contents) { filesystem_error("du"); return; }
        bytes = (u64)kstrnlen(contents, FS_FILE_MAX_BYTES);
        files = 1;
    } else if (fs_is_directory(path)) du_walk(path, 0, &bytes, &files);
    else { puts("du: path not found\n"); return; }
    puts("Files: "); number(files); puts("  Text data: "); number(bytes);
    puts(" bytes  Path: /"); puts(path); puts("\n");
}


static int shell_mkdir_parents(const char *relative_path) {
    if (fs_mkdir_p(relative_path) < 0) {
        filesystem_error("mkdir");
        return -1;
    }
    return 0;
}

static void command(const char *line) {
    char command_name[32];
    char first[FS_PATH_CAPACITY];
    char second[FS_PATH_CAPACITY];
    char source[FS_PATH_CAPACITY];
    char destination[FS_PATH_CAPACITY];
    char path[FS_PATH_CAPACITY];
    const char *cursor = line;
    int first_status;
    if (word(&cursor, command_name, sizeof(command_name)) <= 0) return;

    if (eq(command_name, "help") || eq(command_name, "?")) {
        char topic[24];
        if (word(&cursor, topic, sizeof(topic)) > 0) help_topic(topic);
        else help();
        return;
    }
    if (eq(command_name, "which")) {
        char name[FS_PATH_CAPACITY];
        char program[FS_PATH_CAPACITY];
        if (word(&cursor, name, sizeof(name)) <= 0 ||
            word(&cursor, program, sizeof(program)) != 0) {
            puts("usage: which PROGRAM\n"); return;
        }
        if (shell_is_builtin(name)) { puts(name); puts(": shell built-in\n"); return; }
        if (shell_find_program(name, program) == 0) {
            puts("/"); puts(program); puts("\n"); return;
        }
        puts("which: not found: "); puts(name); puts("\n");
        return;
    }
    if (eq(command_name, "cc")) { command_cc(cursor); return; }
    if (eq(command_name, "run")) { command_run(cursor); return; }
    if (eq(command_name, "cinfo")) { command_cinfo(); return; }
    if (eq(command_name, "clear") || eq(command_name, "cls")) {
        puts("\033[0m\033[2J\033[H"); return;
    }
    if (eq(command_name, "apps")) { terminal_apps_list(); return; }
    if (eq(command_name, "calc")) { skip_spaces(&cursor); terminal_calculator_run(cursor); return; }
    if (eq(command_name, "browse")) {
        char path[FS_PATH_CAPACITY];
        first_status = word(&cursor, first, sizeof(first));
        if (first_status < 0) { puts("browse: path is too long\n"); return; }
        if (first_status == 0) (void)kstrlcpy(path, shell_current_directory, sizeof(path));
        else if (shell_resolve(first, path) < 0) { puts("browse: invalid path\n"); return; }
        terminal_browse_run(path); return;
    }
    if (eq(command_name, "notes")) {
        char path[FS_PATH_CAPACITY];
        first_status = word(&cursor, first, sizeof(first));
        if (first_status < 0) { puts("notes: path is too long\n"); return; }
        if (first_status == 0) (void)kstrlcpy(path, "home/user/documents/quick-notes.md", sizeof(path));
        else if (shell_resolve(first, path) < 0) { puts("notes: invalid path\n"); return; }
        editor_open(path); return;
    }
    if (eq(command_name, "monitor")) { terminal_monitor_run(); return; }
    if (eq(command_name, "arcade")) { terminal_arcade_run(); return; }
    if (eq(command_name, "whoami")) { const char *user = env_get("USER"); puts(user ? user : "local-user"); puts("\n"); return; }
    if (eq(command_name, "hostname")) { const char *host = env_get("HOSTNAME"); puts(host ? host : DENOS_HOSTNAME); puts("\n"); return; }
    if (eq(command_name, "uname")) {
        puts(DENOS_NAME " " DENOS_HOSTNAME " " DENOS_VERSION " x86_64 custom-kernel\n"); return;
    }
    if (eq(command_name, "ver")) { puts(DENOS_NAME " version " DENOS_VERSION "\n"); return; }
    if (eq(command_name, "about")) {
        puts(DENOS_NAME " " DENOS_VERSION " is an experimental 64-bit operating system.\n");
        puts("It includes a terminal shell, native text apps and ASCII arcade games,\n");
        puts("kernel heap, page allocator, VFS, DenFS, ATA storage, services, IPC and Ethernet.\n");
        puts("This is an experimental hobby OS; it is not Linux-compatible. It does not\n");
        puts("yet provide protected user processes or broad hardware support.\n");
        return;
    }
    if (eq(command_name, "bootargs")) { puts(bootargs_get()[0] ? bootargs_get() : "(none)"); puts("\n"); return; }
    if (eq(command_name, "history")) {
        char item[HISTORY_LINE_CAP];
        puts("COMMAND HISTORY\n");
        for (usize i = history_count(); i > 0; --i) { puts("  "); number((u64)i); puts("  "); if (history_get_newest(i - 1u, item, sizeof(item)) == 0) puts(item); puts("\n"); }
        return;
    }
    if (eq(command_name, "env")) {
        env_entry_t item;
        for (usize i = 0; i < env_count(); ++i) if (env_get_by_index(i, &item) == 0) { puts(item.key); puts("="); puts(item.value); puts("\n"); }
        return;
    }
    if (eq(command_name, "set")) {
        char key[ENV_KEY_CAP];
        if (word(&cursor, key, sizeof(key)) <= 0) { puts("usage: set KEY VALUE\n"); return; }
        skip_spaces(&cursor);
        if (!*cursor || env_set(key, cursor) < 0) puts("set: invalid key/value or environment table full\n");
        return;
    }
    if (eq(command_name, "unset")) {
        if (word(&cursor, first, sizeof(first)) <= 0 || env_unset(first) < 0) puts("unset: variable not found or invalid name\n");
        return;
    }
    if (eq(command_name, "services")) {
        service_info_t item;
        puts("SERVICE                 STATE     TASK  STARTS  INTERVAL(ticks)\n");
        for (usize i = 0; i < service_count(); ++i) if (service_get_by_index(i, &item) == 0) {
            puts(item.name); puts("  "); puts(item.running ? "running   " : "stopped   ");
            number(item.task_id); puts("  "); number(item.starts); puts("      "); number(item.interval); puts("\n");
        }
        return;
    }
    if (eq(command_name, "service")) {
        char action[16]; char name[SERVICE_NAME_CAP]; service_info_t item; int found = 0;
        if (word(&cursor, action, sizeof(action)) <= 0 || word(&cursor, name, sizeof(name)) <= 0) { puts("usage: service start|stop NAME\n"); return; }
        for (usize i = 0; i < service_count(); ++i) {
            if (service_get_by_index(i, &item) == 0 && eq(item.name, name)) {
                found = 1;
                if (eq(action, "start")) { if (service_start(item.id) == 0) puts("service started\n"); else puts("service: start failed\n"); }
                else if (eq(action, "stop")) { if (service_stop(item.id) == 0) puts("service stopped\n"); else puts("service: stop failed\n"); }
                else puts("service: action must be start or stop\n");
                break;
            }
        }
        if (!found) puts("service: service not found\n");
        return;
    }
    if (eq(command_name, "uuid")) { char id[37]; if (uuid_generate_v4(id) == 0) { puts(id); puts("\n"); } return; }
    if (eq(command_name, "random")) { puts("PRNG value (non-cryptographic): "); char value[24]; ksnprintf(value, sizeof(value), "%llx", (unsigned long long)entropy_next()); puts(value); puts("\n"); return; }
    if (eq(command_name, "partitions")) {
        partition_entry_t entries[PARTITION_MAX_MBR];
        int count;
        if (!ata_is_available()) { puts("partitions: ATA disk is unavailable\n"); return; }
        count = partition_scan_mbr(shell_partition_read, 0, fs_disk_sector_count(), entries, PARTITION_MAX_MBR);
        if (count < 0) { puts("partitions: no valid MBR partition table found (disk may use DenFS directly or GPT)\n"); return; }
        puts("NUM  BOOT  TYPE                 START LBA   SECTORS\n");
        for (int i = 0; i < count; ++i) { puts("  "); number((u64)i + 1u); puts("    "); puts(entries[i].bootable ? "yes   " : "no    "); puts(partition_type_name(entries[i].type)); puts("  "); number(entries[i].first_lba); puts("       "); number(entries[i].sectors); puts("\n"); }
        return;
    }
    if (eq(command_name, "sha256") || eq(command_name, "crc32")) {
        const char *contents;
        u8 digest[32]; char hex[65]; char number_text[16];
        if (word(&cursor, first, sizeof(first)) <= 0 || shell_resolve(first, path) < 0 || !(contents = fs_read(path))) { puts("usage: sha256|crc32 FILE (file must be in DenFS)\n"); return; }
        usize length = kstrnlen(contents, FS_FILE_MAX_BYTES);
        if (eq(command_name, "sha256")) { sha256_digest(contents, length, digest); sha256_hex(digest, hex); puts(hex); }
        else { ksnprintf(number_text, sizeof(number_text), "%08x", crc32_compute(contents, length)); puts(number_text); }
        puts("  "); puts(first); puts(" ( "); number((u64)length); puts(" bytes )\n"); return;
    }
    if (eq(command_name, "mounts")) {
        vfs_mount_info_t mount;
        puts("FILESYSTEM  MOUNTPOINT  FLAGS\n");
        for (usize i = 0; i < vfs_mount_count(); ++i) {
            if (vfs_mount_get(i, &mount) < 0) continue;
            puts(mount.name); puts("  /"); puts(mount.path[0] ? mount.path : "");
            puts((mount.flags & VFS_MOUNT_READ_ONLY) ? "  read-only\n" : "  read-write\n");
        }
        return;
    }
    if (eq(command_name, "pwd")) {
        puts("/"); puts(shell_current_directory); puts("\n"); return;
    }
    if (eq(command_name, "ls") || eq(command_name, "dir")) {
        char path[FS_PATH_CAPACITY];
        first_status = word(&cursor, first, sizeof(first));
        if (first_status < 0) { puts("ls: path is too long\n"); return; }
        if (first_status == 0) (void)kstrlcpy(path, shell_current_directory, sizeof(path));
        else if (shell_resolve(first, path) < 0) { puts("ls: invalid path\n"); return; }
        if (first_status > 0 && first[0] == '/') {
            vfs_stat_t stat;
            if (vfs_stat_path(first, &stat) < 0 || stat.type != VFS_TYPE_DIRECTORY) {
                puts("ls: not a directory\n"); return;
            }
            shell_list_vfs(first); return;
        }
        if (!fs_is_directory(path)) { puts("ls: not a directory\n"); return; }
        shell_list_directory(path); return;
    }
    if (eq(command_name, "cd")) {
        char path[FS_PATH_CAPACITY];
        first_status = word(&cursor, first, sizeof(first));
        if (first_status <= 0) (void)kstrlcpy(first, "/home/user", sizeof(first));
        if (shell_resolve(first, path) < 0 || !fs_is_directory(path)) {
            puts("cd: directory not found or path is invalid\n"); return;
        }
        (void)kstrlcpy(shell_current_directory, path, sizeof(shell_current_directory));
        char pwd[FS_PATH_CAPACITY + 2u];
        (void)ksnprintf(pwd, sizeof(pwd), "/%s", shell_current_directory);
        (void)env_set("PWD", pwd);
        return;
    }
    if (eq(command_name, "mkdir") || eq(command_name, "rmdir") ||
        eq(command_name, "touch") || eq(command_name, "new") || eq(command_name, "rm") ||
        eq(command_name, "del") || eq(command_name, "cat") || eq(command_name, "type") ||
        eq(command_name, "edit") || eq(command_name, "vi") || eq(command_name, "vim")) {
        char path[FS_PATH_CAPACITY];
        if (word(&cursor, first, sizeof(first)) <= 0) {
            puts("usage: command PATH\n"); return;
        }
        if (eq(command_name, "mkdir")) {
            int parents = eq(first, "-p");
            if (parents) {
                int path_status = word(&cursor, second, sizeof(second));
                if (path_status <= 0 || word(&cursor, destination, sizeof(destination)) != 0) {
                    puts("usage: mkdir [-p] PATH\n"); return;
                }
                if (shell_resolve(second, path) < 0) { puts("mkdir: invalid path or path too long\n"); return; }
                if (shell_mkdir_parents(path) < 0) return;
                return;
            }
            if (word(&cursor, second, sizeof(second)) != 0) {
                puts("usage: mkdir [-p] PATH\n"); return;
            }
            if (shell_resolve(first, path) < 0) { puts("mkdir: invalid path or path too long\n"); return; }
            if ((first[0] == '/' ? vfs_mkdir(first) : fs_mkdir(path)) < 0) filesystem_error("mkdir");
            return;
        }
        if (shell_resolve(first, path) < 0) { puts("path is invalid or too long\n"); return; }
        if (eq(command_name, "rmdir")) {
            if (first[0] == '/' ? vfs_remove(first) < 0 : (!fs_is_directory(path) || fs_remove(path) < 0)) filesystem_error("rmdir");
        } else if (eq(command_name, "touch") || eq(command_name, "new")) {
            if (fs_touch(path) < 0) filesystem_error("touch");
        } else if (eq(command_name, "rm") || eq(command_name, "del")) {
            if ((first[0] == '/' ? vfs_remove(first) : fs_remove(path)) < 0) filesystem_error("rm");
        } else if (eq(command_name, "cat") || eq(command_name, "type")) {
            if (first[0] == '/') {
                char contents[4096]; usize length = 0;
                if (vfs_read(first, contents, sizeof(contents), &length) == 0) puts(contents);
                else { puts("cat: "); puts(vfs_last_error()); puts("\n"); }
            } else {
                const char *contents = fs_read(path);
                if (contents) puts(contents); else filesystem_error("cat");
            }
        } else {
            editor_open(path);
        }
        return;
    }
    if (eq(command_name, "cp") || eq(command_name, "mv")) {
        if (word(&cursor, first, sizeof(first)) <= 0 || word(&cursor, second, sizeof(second)) <= 0) {
            puts("usage: cp SOURCE DESTINATION\n"); return;
        }
        if (shell_resolve(first, source) < 0 || shell_resolve(second, destination) < 0) {
            puts("path is invalid or too long\n"); return;
        }
        if ((eq(command_name, "cp") ? fs_copy(source, destination) : fs_move(source, destination)) < 0)
            filesystem_error(command_name);
        return;
    }
    if (eq(command_name, "write")) {
        char path[FS_PATH_CAPACITY];
        if (word(&cursor, first, sizeof(first)) <= 0 || shell_resolve(first, path) < 0) {
            puts("usage: write FILE TEXT\n"); return;
        }
        skip_spaces(&cursor);
        if (!*cursor) { puts("usage: write FILE TEXT\n"); return; }
        if ((first[0] == '/' ? vfs_write(first, cursor) : fs_write(path, cursor)) == 0) {
            if (fs_is_persistent()) puts("saved to persistent disk\n");
            else puts("saved to RAM only; changes will be lost after reboot\n");
        } else filesystem_error("write");
        return;
    }
    if (eq(command_name, "net") || eq(command_name, "ifconfig")) {
        net_status_t status;
        if (net_get_status(&status) < 0) { puts("Network: no supported Intel E1000 adapter detected\n"); return; }
        puts("Interface: e1000\nLink: "); puts(status.link_up ? "up\n" : "down\n");
        puts("MAC: ");
        for (int i=0;i<6;i++) { char hex[]="0123456789abcdef"; char pair[3]={hex[status.mac[i]>>4],hex[status.mac[i]&15],0}; puts(pair); if (i<5) puts(":"); }
        puts("\nIPv4: ");
        for (int i=0;i<4;i++) { number(status.ipv4[i]); if (i<3) puts("."); }
        puts("\nMask: ");
        for (int i=0;i<4;i++) { number(status.netmask[i]); if (i<3) puts("."); }
        puts("\nGateway: ");
        for (int i=0;i<4;i++) { number(status.gateway[i]); if (i<3) puts("."); }
        puts("\nDNS: ");
        for (int i=0;i<4;i++) { number(status.dns[i]); if (i<3) puts("."); }
        puts("\nFrames RX/TX: "); number(status.rx_frames); puts("/"); number(status.tx_frames);
        puts("\nConfiguration: "); puts(status.dhcp_configured ? "DHCP lease\n" : "static fallback profile\n");
        return;
    }
    if (eq(command_name, "dhcp")) {
        puts("Requesting network settings with DHCP...\n");
        if (net_dhcp_configure(500u) < 0) puts("dhcp: no valid lease received; current network settings were kept\n");
        else puts("DHCP configuration applied. Run 'net' to inspect the lease.\n");
        return;
    }
    if (eq(command_name, "dns")) {
        u8 address[4];
        if (word(&cursor, first, sizeof(first)) <= 0) { puts("usage: dns HOST\n"); return; }
        skip_spaces(&cursor);
        if (*cursor) { puts("usage: dns HOST\n"); return; }
        if (net_dns_resolve(first, address, 300u) < 0) { puts("dns: lookup failed\n"); return; }
        puts(first); puts(" -> ");
        for (u32 i = 0; i < 4; ++i) { number(address[i]); if (i < 3) puts("."); }
        puts("\n");
        return;
    }
    if (eq(command_name, "web")) {
        char url[512];
        int result;
        if (word(&cursor, url, sizeof(url)) <= 0) { puts("usage: web https://HOST[:PORT]/PATH (or http:// for unencrypted HTTP)\n"); return; }
        skip_spaces(&cursor);
        if (*cursor) { puts("web: quote the URL if it contains spaces; only one URL is accepted\n"); return; }
        result = net_http_get(url);
        if (result < 0) puts("\nweb: page load failed\n");
        return;
    }
    if (eq(command_name, "ping")) {
        u8 address[4];
        if (word(&cursor, first, sizeof(first)) <= 0) { puts("usage: ping HOST\n"); return; }
        skip_spaces(&cursor);
        if (*cursor) { puts("usage: ping HOST\n"); return; }
        if (parse_ipv4(first, address) < 0 && net_dns_resolve(first, address, 300u) < 0) {
            puts("ping: could not resolve host\n"); return;
        }
        puts("PING: waiting for ICMP echo reply...\n");
        int result=net_ping(address,300u);
        puts(result==0 ? "Reply received\n" : result==-1 ? "Network interface unavailable\n" : result==-2 ? "ARP resolution timed out\n" : "Request timed out or transmit failed\n");
        return;
    }
    if (eq(command_name, "date")) { command_date(); return; }
    if (eq(command_name, "devices")) { command_devices(); return; }
    if (eq(command_name, "bootlog")) { command_bootlog(); return; }
    if (eq(command_name, "cpuinfo")) { command_cpuinfo(); return; }
    if (eq(command_name, "beep")) { command_beep(); return; }
    if (eq(command_name, "hash")) {
        if (word(&cursor, first, sizeof(first)) <= 0 || shell_resolve(first, path) < 0) {
            puts("usage: hash FILE\n"); return;
        }
        command_hash(path); return;
    }
    if (eq(command_name, "df")) {
        puts("Filesystem  Type       Used(B)  Reserved(B)  Free(B)  Total(B)\n");
        puts("rootfs      "); puts(fs_is_persistent() ? "ata-pio     " : "ram-fs      ");
        number((u64)fs_bytes_used()); puts("     "); number((u64)fs_storage_reserved());
        puts("        "); number((u64)fs_storage_free()); puts("    ");
        number((u64)fs_storage_capacity()); puts("\nBacking disk: ");
        puts(fs_disk_model()); puts(" ( "); number((u64)fs_disk_size_bytes()); puts(" bytes )\n");
        return;
    }
    if (eq(command_name, "diskinfo") || eq(command_name, "disk")) {
        puts("Storage device: "); puts(fs_disk_model()); puts("\nTransport: legacy IDE PIO\nSectors: ");
        number((u64)fs_disk_sector_count()); puts("\nCapacity: ");
        number((u64)fs_disk_size_bytes()); puts(" bytes\nFilesystem: ");
        if (fs_is_persistent()) puts("persistent DenFS v4 on ATA\n");
        else {
            puts("temporary RAM fallback\nReason: ");
            puts(fs_storage_name());
            puts("\n");
        }
        return;
    }
    if (eq(command_name, "uptime")) {
        puts("Uptime: "); number(clock_seconds()); puts(" seconds ( "); number(clock_millis()); puts(" ms total )\n"); return;
    }
    if (eq(command_name, "free") || eq(command_name, "meminfo") || eq(command_name, "mem")) {
        heap_diagnostics_t stats;
        if (memory_diagnostics(&stats) < 0) { puts("meminfo: heap accounting is inconsistent\n"); return; }
        puts("Kernel heap used: "); number((u64)stats.used_bytes); puts(" / ");
        number((u64)stats.total_bytes); puts(" bytes\nReusable: ");
        number((u64)stats.free_bytes); puts(" bytes\n");
        return;
    }
    if (eq(command_name, "pmm")) {
        pmm_stats_t stats;
        if (pmm_get_stats(&stats) < 0) { puts("pmm: unavailable\n"); return; }
        puts("Physical pages free: "); number(stats.free_pages); puts("\nAllocated: ");
        number(stats.allocated_pages); puts("\nReserved: "); number(stats.reserved_pages); puts("\n");
        return;
    }
    if (eq(command_name, "paging") || eq(command_name, "vmstat")) {
        vmm_stats_t stats;
        if (vmm_get_stats(&stats) < 0) {
            puts("paging: page-table manager unavailable\n");
            return;
        }
        puts("Virtual memory manager\n");
        puts("  Active root       : current CR3 (bootstrap identity map retained)\n");
        puts("  Paging mode       : x86-64 four-level\n");
        puts("  NX protection     : "); puts(stats.nx_enabled ? "enabled\n" : "unavailable\n");
        puts("  Supervisor WP     : "); puts(stats.write_protect_enabled ? "enabled\n" : "unavailable\n");
        puts("  Global pages      : "); puts(stats.global_pages_enabled ? "enabled\n" : "unavailable\n");
        puts("  Page-table pages  : "); number(stats.page_table_pages); puts("\n");
        puts("  Mappings added    : "); number(stats.mappings_created); puts("\n");
        puts("  Mappings removed  : "); number(stats.mappings_removed); puts("\n");
        puts("  Failed operations : "); number(stats.failed_operations); puts("\n");
        puts("  Note: single CR3 only; no process address spaces or demand paging.\n");
        puts("  Empty dynamic page-table levels are reclaimed; data frames remain caller-owned.\n");
        puts("  Writable leaf pages must be NX when NX is available (W^X policy).\n");
        {
            vmalloc_stats_t vm;
            if (vmalloc_get_stats(&vm) == VMALLOC_OK) {
                puts("Kernel virtual allocator\n");
                puts("  Window capacity   : "); number(vm.total_bytes); puts(" bytes\n");
                puts("  Used / free       : "); number(vm.used_bytes); puts(" / ");
                number(vm.free_bytes); puts(" bytes\n");
                puts("  Live allocations  : "); number(vm.live_allocations); puts("\n");
                puts("  Mapped pages      : "); number(vm.pages_mapped); puts("\n");
                puts("  Allocation fails  : "); number(vm.allocation_failures); puts("\n");
                puts("  Quarantined       : "); number(vm.quarantined_allocations);
                puts(" allocations / "); number(vm.quarantined_bytes); puts(" bytes\n");
            } else puts("Kernel virtual allocator: unavailable\n");
        }
        return;
    }
    if (eq(command_name, "tasks") || eq(command_name, "ps")) {
        scheduler_task_info_t task;
        puts("ID   STATE     PRI  RUNS       NAME\n");
        for (u32 i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
            if (scheduler_get_by_index(i, &task) < 0) break;
            number(task.id); puts("   ");
            puts(task.state == SCHEDULER_READY ? "ready     " :
                 task.state == SCHEDULER_SLEEPING ? "sleeping  " : "stopped   ");
            number(task.priority); puts("    "); number(task.runs); puts("   ");
            puts(task.name); puts("\n");
        }
        puts("Active tasks: "); number(scheduler_count());
        puts("  Total runs: "); number(scheduler_total_runs()); puts("\n");
        return;
    }
    if (eq(command_name, "events")) {
        kernel_event_t event;
        kernel_event_stats_t stats;
        u32 shown = 0;
        puts("TICK       TYPE             SOURCE  CODE  VALUE\n");
        while (shown < 16u && event_poll(&event) == 0) {
            number(event.tick); puts("   "); puts(event_type_name(event.type)); puts("   ");
            number(event.source); puts("      "); number(event.code); puts("   ");
            number(event.value); puts("\n");
            ++shown;
        }
        event_get_stats(&stats);
        puts("Pending: "); number(stats.pending); puts("  Dropped: ");
        number(stats.dropped); puts("\n");
        return;
    }
    if (eq(command_name, "ipc")) {
        scheduler_task_info_t task;
        ipc_endpoint_stats_t stats;
        puts("TASK  PENDING  SENT  RECEIVED  DROPPED\n");
        for (u32 i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
            if (scheduler_get_by_index(i, &task) < 0) break;
            if (ipc_endpoint_get_stats(task.id, &stats) != IPC_OK) continue;
            number(stats.task_id); puts("     "); number(stats.pending); puts("       ");
            number(stats.sent); puts("    "); number(stats.received); puts("       ");
            number(stats.dropped); puts("\n");
        }
        puts("Endpoints: "); number(ipc_endpoint_count()); puts("\n");
        return;
    }
    if (eq(command_name, "sysinfo") || eq(command_name, "sys") || eq(command_name, "neofetch")) {
        system_info(); return;
    }
    if (eq(command_name, "wc")) {
        if (word(&cursor, first, sizeof(first)) <= 0 || shell_resolve(first, path) < 0) { puts("usage: wc FILE\n"); return; }
        command_wc(path); return;
    }
    if (eq(command_name, "grep")) {
        if (word(&cursor, first, sizeof(first)) <= 0 || word(&cursor, second, sizeof(second)) <= 0 || shell_resolve(second, path) < 0) { puts("usage: grep PATTERN FILE\n"); return; }
        command_grep(first, path); return;
    }
    if (eq(command_name, "head") || eq(command_name, "tail")) {
        u32 count = 10u;
        int is_tail = eq(command_name, "tail");
        if (word(&cursor, first, sizeof(first)) <= 0) { puts("usage: head|tail [-n COUNT] FILE\n"); return; }
        if (eq(first, "-n")) {
            if (word(&cursor, first, sizeof(first)) <= 0 || parse_u32(first, &count) < 0 || !count || count > 1000u || word(&cursor, second, sizeof(second)) <= 0) { puts("head/tail: COUNT must be 1..1000\n"); return; }
            if (shell_resolve(second, path) < 0) { puts("head/tail: invalid path\n"); return; }
        } else if (shell_resolve(first, path) < 0) { puts("head/tail: invalid path\n"); return; }
        command_lines(path, count, is_tail); return;
    }
    if (eq(command_name, "hexdump")) {
        if (word(&cursor, first, sizeof(first)) <= 0 || shell_resolve(first, path) < 0) { puts("usage: hexdump FILE\n"); return; }
        command_hexdump(path); return;
    }
    if (eq(command_name, "stat")) {
        if (word(&cursor, first, sizeof(first)) <= 0 || shell_resolve(first, path) < 0) { puts("usage: stat PATH\n"); return; }
        command_stat(path); return;
    }
    if (eq(command_name, "tree")) {
        if (word(&cursor, first, sizeof(first)) <= 0) {
            kstrlcpy(path, shell_current_directory, sizeof(path));
        } else if (shell_resolve(first, path) < 0) {
            puts("tree: invalid path\n"); return;
        }
        tree_walk(path, 0);
        return;
    }
    if (eq(command_name, "find")) { command_find(cursor); return; }
    if (eq(command_name, "du")) { command_du(cursor); return; }
    if (eq(command_name, "echo")) { skip_spaces(&cursor); puts(cursor); puts("\n"); return; }
    if (eq(command_name, "reboot")) { puts("Rebooting...\n"); cpu_reboot(); }
    if (eq(command_name, "halt") || eq(command_name, "poweroff")) { puts("System halted. You may close QEMU.\n"); cpu_halt(); }
    puts("command not found: "); puts(command_name); puts(". Type 'help'.\n");
}

static void shell_expand(const char *input, char *output, usize capacity) {
    usize used = 0;
    if (!output || !capacity) return;
    while (input && *input && used + 1u < capacity) {
        if (*input == '$') {
            char key[ENV_KEY_CAP]; usize key_length = 0; const char *value;
            ++input;
            while (((*input >= 'A' && *input <= 'Z') || (*input >= 'a' && *input <= 'z') ||
                    (*input >= '0' && *input <= '9') || *input == '_') && key_length + 1u < sizeof(key))
                key[key_length++] = *input++;
            key[key_length] = '\0';
            value = key_length ? env_get(key) : 0;
            if (value) { while (*value && used + 1u < capacity) output[used++] = *value++; }
            else { output[used++] = '$'; for (usize i = 0; i < key_length && used + 1u < capacity; ++i) output[used++] = key[i]; }
        } else output[used++] = *input++;
    }
    output[used] = '\0';
}


static void shell_print_prompt(void) {
    const char *user = env_get("USER");
    const char *host = env_get("HOSTNAME");
    puts("\033[1;32m"); puts(user ? user : "user"); puts("\033[0m@");
    puts("\033[1;36m"); puts(host ? host : DENOS_HOSTNAME); puts("\033[0m:");
    puts("\033[1;34m/"); puts(shell_current_directory); puts("\033[0m$ ");
}

static void shell_redraw_line(const char *line, usize cursor) {
    usize length = kstrnlen(line, 512u);
    puts("\r\033[K");
    shell_print_prompt();
    puts(line);
    if (cursor < length) {
        char movement[24];
        ksnprintf(movement, sizeof(movement), "\033[%uD", (unsigned)(length - cursor));
        puts(movement);
    }
}

static int shell_prefix_match(const char *candidate, const char *prefix, usize length) {
    for (usize i = 0; i < length; ++i)
        if (!candidate[i] || candidate[i] != prefix[i]) return 0;
    return 1;
}

static void shell_insert_text(char *line, usize capacity, usize *length,
                              usize *cursor, const char *text, int add_space) {
    usize add = kstrlen(text);
    if (add_space) add++;
    if (*length + add + 1u > capacity) return;
    for (usize i = *length + 1u; i > *cursor; --i) line[i + add - 1u] = line[i - 1u];
    for (usize i = 0; text[i]; ++i) line[*cursor + i] = text[i];
    if (add_space) line[*cursor + add - 1u] = ' ';
    *length += add;
    *cursor += add;
}

static void shell_tab_complete(char *line, usize capacity,
                               usize *length, usize *cursor) {
    static const char *commands[] = {
        "help","ls","cd","pwd","mkdir","rmdir","touch","cat","edit","vim",
        "write","cp","mv","rm","cc","run","cinfo","grep","wc","head","tail",
        "stat","hexdump","tree","find","du","which","sha256","crc32","hash","df","diskinfo","partitions",
        "mounts","devices","cpuinfo","sysinfo","uptime","date","free","pmm","paging","vmstat","tasks",
        "services","service","ipc","events","net","ping","dhcp","dns","web","history","env","set","unset",
        "whoami","hostname","uname","ver","about","clear","beep","reboot","halt","uuid",
        "random","bootargs","bootlog","echo","apps","browse","calc","notes","monitor","arcade","!!"
    };
    char prefix[FS_PATH_CAPACITY];
    char matches[12][FS_PATH_CAPACITY];
    int match_count = 0;
    usize start;
    int command_position;
    if (!line || !length || !cursor || capacity < 2u) return;
    /* Keep the editor's internal cursor/length invariant local to completion
     * too. This prevents a stale history/completion position from indexing
     * before the initialized portion of the command buffer. */
    if (*length >= capacity) *length = capacity - 1u;
    line[*length] = 0;
    if (*cursor > *length) *cursor = *length;
    start = *cursor;
    while (start && line[start - 1u] != ' ' && line[start - 1u] != '\t') start--;
    if (*cursor < *length) { serial_putc('\a'); return; }
    usize prefix_length = *cursor - start;
    if (!prefix_length || prefix_length >= sizeof(prefix)) return;
    for (usize i = 0; i < prefix_length; ++i) prefix[i] = line[start + i];
    prefix[prefix_length] = 0;
    command_position = start == 0;
    if (command_position) {
        for (usize i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
            if (shell_prefix_match(commands[i], prefix, prefix_length) && match_count < 12) {
                kstrlcpy(matches[match_count++], commands[i], sizeof(matches[0]));
            }
        }
    } else {
        usize entries = fs_entry_count_in_dir(shell_current_directory);
        for (usize i = 0; i < entries && match_count < 12; ++i) {
            char name[FS_PATH_CAPACITY];
            usize size = 0;
            int type = 0;
            if (fs_get_entry_in_dir(shell_current_directory, i, name, sizeof(name), &size, &type) < 0) continue;
            if (shell_prefix_match(name, prefix, prefix_length)) {
                kstrlcpy(matches[match_count], name, sizeof(matches[match_count]));
                usize n = kstrlen(matches[match_count]);
                if (type == FS_ENTRY_DIRECTORY && n + 1u < sizeof(matches[match_count])) {
                    matches[match_count][n++] = '/'; matches[match_count][n] = 0;
                }
                match_count++;
            }
        }
    }
    if (!match_count) { serial_putc('\a'); return; }
    if (match_count == 1) {
        int is_directory = matches[0][kstrlen(matches[0]) - 1u] == '/';
        shell_insert_text(line, capacity, length, cursor, matches[0] + prefix_length,
                          !is_directory);
        shell_redraw_line(line, *cursor);
        return;
    }
    serial_putc('\n');
    for (int i = 0; i < match_count; ++i) {
        puts(matches[i]);
        puts(i + 1 == match_count ? "\n" : "   ");
    }
    shell_redraw_line(line, *cursor);
}

static void shell_readline(char *line, usize capacity) {
    usize length = 0, cursor = 0;
    int history_offset = -1;
    char draft[512] = {0};
    if (!line || capacity < 2u) return;
    /* Initialize the complete editable line so defensive cursor bounds and
     * completion scans never observe indeterminate stack bytes. */
    for (usize i = 0; i < capacity; ++i) line[i] = 0;
    for (;;) {
        int key = serial_getkey();
        if (key == '\r' || key == '\n') {
            serial_putc('\n');
            line[length] = '\0';
            return;
        }
        if (key == 8 || key == 127) {
            if (cursor) {
                for (usize i = cursor; i < length; ++i) line[i - 1u] = line[i];
                --cursor; --length; line[length] = '\0'; shell_redraw_line(line, cursor);
            }
            continue;
        }
        if (key == KEY_DELETE) {
            if (cursor < length) {
                for (usize i = cursor + 1u; i < length; ++i) line[i - 1u] = line[i];
                --length; line[length] = '\0'; shell_redraw_line(line, cursor);
            }
            continue;
        }
        if (key == KEY_LEFT) { if (cursor) { cursor--; shell_redraw_line(line, cursor); } continue; }
        if (key == KEY_RIGHT) { if (cursor < length) { cursor++; shell_redraw_line(line, cursor); } continue; }
        if (key == KEY_HOME || key == 1) { cursor = 0; shell_redraw_line(line, cursor); continue; }
        if (key == KEY_END || key == 5) { cursor = length; shell_redraw_line(line, cursor); continue; }
        if (key == 21) { /* Ctrl-U: clear the current command line. */
            length = cursor = 0; line[0] = 0; shell_redraw_line(line, cursor); continue;
        }
        if (key == 11) { /* Ctrl-K: clear from cursor to end. */
            length = cursor; line[length] = 0; shell_redraw_line(line, cursor); continue;
        }
        if (key == '\t') { shell_tab_complete(line, capacity, &length, &cursor); continue; }
        if (key == KEY_UP || key == KEY_DOWN) {
            char selected[HISTORY_LINE_CAP];
            if (key == KEY_UP) {
                if (history_offset < 0) { kstrlcpy(draft, line, sizeof(draft)); history_offset = 0; }
                else if ((usize)(history_offset + 1) < history_count()) ++history_offset;
                if (history_offset >= 0 && history_get_newest((usize)history_offset, selected, sizeof(selected)) == 0)
                    kstrlcpy(line, selected, capacity);
            } else if (history_offset >= 0) {
                if (history_offset > 0) { --history_offset; (void)history_get_newest((usize)history_offset, line, capacity); }
                else { history_offset = -1; kstrlcpy(line, draft, capacity); }
            }
            length = kstrnlen(line, capacity - 1u); cursor = length;
            shell_redraw_line(line, cursor);
            continue;
        }
        if (key >= 32 && key < 127 && length + 1u < capacity) {
            for (usize i = length + 1u; i > cursor; --i) line[i] = line[i - 1u];
            line[cursor++] = (char)key;
            length++;
            line[length] = '\0';
            shell_redraw_line(line, cursor);
        }
    }
}

void shell_run(void) {
    char line[512];
    char expanded[512];

    puts("DenOS interactive shell\nType help for a command list. Use arrows, Home/End, Tab completion, and Up/Down history.\n\n");
    (void)env_set("PWD", "/home/user");
    for (;;) {
        scheduler_run();
        shell_print_prompt();
        shell_readline(line, sizeof(line));
        if (!line[0]) continue;
        if (eq(line, "!!")) {
            char previous[HISTORY_LINE_CAP];
            if (history_get_newest(0, previous, sizeof(previous)) == 0) {
                puts(previous); puts("\n"); shell_expand(previous, expanded, sizeof(expanded)); command(expanded);
            } else puts("history: no previous command\n");
        } else {
            (void)history_add(line);
            shell_expand(line, expanded, sizeof(expanded));
            command(expanded);
        }
    }
}
