
#include "io.h"
#include "serial.h"
#ifndef DENOS_TEST_IO
#include "scheduler.h"
#endif

/* ==========================================
   DenOS VGA + Serial Terminal Driver
   80x25 VGA text mode
   PS/2 keyboard polling
   COM1 serial debugging
   ========================================== */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

#define VGA_COLOR  0x07

static volatile u16 *vga = (volatile u16 *)VGA_MEMORY;

static usize cursor_x = 0;
static usize cursor_y = 0;
static u8 vga_attribute = VGA_COLOR;
static int uart_mirror;
static int uart_present;

static int shift_pressed = 0;
static int caps_lock = 0;
static int ctrl_pressed = 0;
static int keyboard_extended_pending;

/* ==========================================
   UART / Serial
   ========================================== */

static int tx_ready(void) {
    return inb(0x3FD) & 0x20;
}

static void uart_put_raw(char c) {
    if (!uart_present) return;

    /* A broken or disabled COM1 must not stall VGA output forever. */
    for (u32 wait = 0; wait < 1000000u; wait++) {
        if (tx_ready()) {
            outb(0x3F8, (u8)c);
            return;
        }
        __asm__ volatile("pause");
    }

    uart_present = 0;
    uart_mirror = 0;
}

void serial_init(void) {
    /* Initialize COM1 */

    uart_present = 0;
    uart_mirror = 0;
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);

    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);

    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);

    /* 16550-compatible scratch-register test distinguishes a real UART
       from an unmapped port before enabling serial mirroring. */
    outb(0x3FF, 0x5A);
    if (inb(0x3FF) == 0x5A) {
        outb(0x3FF, 0xA5);
        uart_present = (inb(0x3FF) == 0xA5);
    }
    uart_mirror = uart_present;

    /* Initialize VGA */

    cursor_x = 0;
    cursor_y = 0;

    /* Make the hardware cursor a clearly visible block cursor. */
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x00); /* Start scan line; bit 5 clear keeps it visible. */
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F); /* End scan line: full-height block. */

    for (usize i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = ((u16)vga_attribute << 8) | ' ';
    }

    /* Initialize keyboard state */

    shift_pressed = 0;
    caps_lock = 0;
    ctrl_pressed = 0;
    keyboard_extended_pending = 0;

    /* Enable PS/2 keyboard */

    outb(0x64, 0xAE);
}

/* ==========================================
   VGA cursor
   ========================================== */

static void update_cursor(void) {
    u16 pos = (u16)(cursor_y * VGA_WIDTH + cursor_x);

    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

static void vga_clear(void);

void serial_cursor_enable(int enabled) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, enabled ? 0x00 : 0x20);
    update_cursor();
}

/* ==========================================
   Clear terminal
   ========================================== */

static void vga_clear(void) {
    for (usize i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = ((u16)vga_attribute << 8) | ' ';
    }

    cursor_x = 0;
    cursor_y = 0;

    update_cursor();
}

void serial_video_reset(void) {
    /* The graphics driver restores VGA text mode before calling this hook.
       Keep the UART state intact; only reset the software text-console view. */
    vga_attribute = VGA_COLOR;
    vga_clear();
    serial_cursor_enable(1);
}

/* ==========================================
   Scroll terminal
   ========================================== */

static void vga_scroll(void) {
    for (usize y = 1; y < VGA_HEIGHT; y++) {
        for (usize x = 0; x < VGA_WIDTH; x++) {
            vga[(y - 1) * VGA_WIDTH + x] =
                vga[y * VGA_WIDTH + x];
        }
    }

    for (usize x = 0; x < VGA_WIDTH; x++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            ((u16)vga_attribute << 8) | ' ';
    }

    cursor_y = VGA_HEIGHT - 1;
}

/* ==========================================
   VGA character output
   ========================================== */

static void vga_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    }
    else if (c == '\r') {
        cursor_x = 0;
    }
    else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;

            vga[cursor_y * VGA_WIDTH + cursor_x] =
                ((u16)vga_attribute << 8) | ' ';
        }
    }
    else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;

        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    else if ((u8)c >= 32) {
        vga[cursor_y * VGA_WIDTH + cursor_x] =
            ((u16)vga_attribute << 8) | (u8)c;

        cursor_x++;

        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= VGA_HEIGHT) {
        vga_scroll();
    }

    update_cursor();
}

/* ==========================================
   Public character output
   ========================================== */

void serial_set_mirror(int enabled) {
    uart_mirror = (enabled && uart_present) ? 1 : 0;
}

void serial_putc(char c) {
    if (uart_mirror) {
        if (c == '\n') { uart_put_raw('\r'); uart_put_raw('\n'); }
        else uart_put_raw(c);
    }
    vga_putc(c);
}

/* ==========================================
   ANSI terminal support
   The VGA screen is the primary local display. ANSI sequences are also
   mirrored to COM1 for terminal/serial debugging.
   ========================================== */

static void vga_set_position(int row, int col) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
    cursor_y = (usize)row;
    cursor_x = (usize)col;
    update_cursor();
}

static void vga_erase_line(int mode) {
    usize start = cursor_y * VGA_WIDTH;
    usize first = 0;
    usize last = VGA_WIDTH;
    if (mode == 0) first = cursor_x;
    else if (mode == 1) last = cursor_x + 1;
    for (usize x = first; x < last; x++)
        vga[start + x] = ((u16)vga_attribute << 8) | ' ';
}

static void vga_erase_display(int mode) {
    usize pos = cursor_y * VGA_WIDTH + cursor_x;
    usize total = VGA_WIDTH * VGA_HEIGHT;
    if (mode == 2) {
        vga_clear();
    } else if (mode == 0) {
        for (usize i = pos; i < total; i++)
            vga[i] = ((u16)vga_attribute << 8) | ' ';
    } else if (mode == 1) {
        for (usize i = 0; i <= pos && i < total; i++)
            vga[i] = ((u16)vga_attribute << 8) | ' ';
    }
}

static void vga_sgr(const int *params, int count) {
    static const u8 ansi_fg[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    static const u8 ansi_bright_fg[8] = {8, 12, 10, 14, 9, 13, 11, 15};
    int fg = 7;
    int bg = 0;
    int bold = 0;
    /* Recover the current VGA colors before applying partial SGR updates. */
    fg = vga_attribute & 0x0F;
    bg = (vga_attribute >> 4) & 0x07;
    for (int i = 0; i < count; i++) {
        int p = params[i];
        if (p == 0) { fg = 7; bg = 0; bold = 0; }
        else if (p == 1) bold = 1;
        else if (p == 22) bold = 0;
        else if (p == 7) { int swap = fg; fg = bg; bg = swap; }
        else if (p == 27) { int swap = fg; fg = bg; bg = swap; }
        else if (p >= 30 && p <= 37) fg = ansi_fg[p - 30];
        else if (p == 39) fg = 7;
        else if (p >= 40 && p <= 47) bg = ansi_fg[p - 40];
        else if (p == 49) bg = 0;
        else if (p >= 90 && p <= 97) fg = ansi_bright_fg[p - 90];
        else if (p >= 100 && p <= 107) bg = ansi_fg[p - 100];
    }
    if (bold && fg < 8) fg += 8;
    vga_attribute = (u8)(((bg & 7) << 4) | (fg & 15));
}

void serial_write(const char *s) {
    while (*s) {
        if ((u8)*s == 27) {
            const char *seq = s;
            s++;
            if (*s == '[') {
                int params[8] = {0,0,0,0,0,0,0,0};
                int count = 0;
                int value = 0;
                int have_value = 0;
                int private_mode = 0;
                s++;
                if (*s == '?') { private_mode = 1; s++; }
                while (*s && !((*s >= 'A' && *s <= 'Z') ||
                               (*s >= 'a' && *s <= 'z'))) {
                    if (*s >= '0' && *s <= '9') {
                        /* Cursor and color parameters are small; keep malformed
                           escape sequences from overflowing this accumulator. */
                        if (value < 1000000)
                            value = value * 10 + (*s - '0');
                        have_value = 1;
                    } else if (*s == ';') {
                        if (count < 8) params[count++] = have_value ? value : 0;
                        value = 0;
                        have_value = 0;
                    }
                    s++;
                }
                if (count < 8) params[count++] = have_value ? value : 0;
                char final = *s;
                if (*s) s++;
                /* Full-screen apps can disable COM1 mirroring to avoid
                   transmitting every screen repaint into the debug log. */
                if (uart_mirror)
                    for (const char *q = seq; q < s; q++) uart_put_raw(*q);
                int n = count ? params[0] : 0;
                if (private_mode) {
                    /* Cursor visibility controls are accepted; VGA BIOS cursor
                       is left enabled so the user always has a position cue. */
                } else if (final == 'J') {
                    vga_erase_display(n);
                } else if (final == 'K') {
                    vga_erase_line(n);
                } else if (final == 'H' || final == 'f') {
                    int row = n ? n - 1 : 0;
                    int col = count > 1 && params[1] ? params[1] - 1 : 0;
                    vga_set_position(row, col);
                } else if (final == 'A') {
                    vga_set_position((int)cursor_y - (n ? n : 1), (int)cursor_x);
                } else if (final == 'B') {
                    vga_set_position((int)cursor_y + (n ? n : 1), (int)cursor_x);
                } else if (final == 'C') {
                    vga_set_position((int)cursor_y, (int)cursor_x + (n ? n : 1));
                } else if (final == 'D') {
                    vga_set_position((int)cursor_y, (int)cursor_x - (n ? n : 1));
                } else if (final == 'G') {
                    vga_set_position((int)cursor_y, (n ? n - 1 : 0));
                } else if (final == 'd') {
                    vga_set_position(n ? n - 1 : 0, (int)cursor_x);
                } else if (final == 'm') {
                    vga_sgr(params, count);
                }
                update_cursor();
                continue;
            }
            /* Unknown ESC sequence: consume the ESC without displaying it. */
            continue;
        }
        serial_putc(*s++);
    }
}

/* ==========================================
   PS/2 Keyboard
   ========================================== */

static const char keymap[128] = {
    [0x01] = 27,
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',

    [0x0C] = '-',
    [0x0D] = '=',
    [0x0E] = '\b',
    [0x0F] = '\t',

    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',

    [0x1A] = '[',
    [0x1B] = ']',

    [0x1C] = '\n',

    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',

    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',

    [0x2B] = '\\',

    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',

    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '/',

    [0x39] = ' '
};

static const char shift_keymap[128] = {
    [0x02] = '!',
    [0x03] = '@',
    [0x04] = '#',
    [0x05] = '$',
    [0x06] = '%',
    [0x07] = '^',
    [0x08] = '&',
    [0x09] = '*',
    [0x0A] = '(',
    [0x0B] = ')',

    [0x0C] = '_',
    [0x0D] = '+',

    [0x10] = 'Q',
    [0x11] = 'W',
    [0x12] = 'E',
    [0x13] = 'R',
    [0x14] = 'T',
    [0x15] = 'Y',
    [0x16] = 'U',
    [0x17] = 'I',
    [0x18] = 'O',
    [0x19] = 'P',

    [0x1A] = '{',
    [0x1B] = '}',

    [0x1E] = 'A',
    [0x1F] = 'S',
    [0x20] = 'D',
    [0x21] = 'F',
    [0x22] = 'G',
    [0x23] = 'H',
    [0x24] = 'J',
    [0x25] = 'K',
    [0x26] = 'L',

    [0x27] = ':',
    [0x28] = '"',
    [0x29] = '~',

    [0x2B] = '|',

    [0x2C] = 'Z',
    [0x2D] = 'X',
    [0x2E] = 'C',
    [0x2F] = 'V',
    [0x30] = 'B',
    [0x31] = 'N',
    [0x32] = 'M',

    [0x33] = '<',
    [0x34] = '>',
    [0x35] = '?'
};

/* ==========================================
   PS/2 keyboard event decoding (set-1 scancodes)
   ========================================== */

static int keyboard_decode_scancode(u8 scancode, int extended, int *key) {
    if (extended) {
        if (scancode & 0x80) {
            if ((scancode & 0x7F) == 0x1D) ctrl_pressed = 0;
            return 0;
        }
        if (scancode == 0x1D) { ctrl_pressed = 1; return 0; }
        switch (scancode) {
            case 0x48: *key = KEY_UP; return 1;
            case 0x50: *key = KEY_DOWN; return 1;
            case 0x4B: *key = KEY_LEFT; return 1;
            case 0x4D: *key = KEY_RIGHT; return 1;
            case 0x47: *key = KEY_HOME; return 1;
            case 0x4F: *key = KEY_END; return 1;
            case 0x53: *key = KEY_DELETE; return 1;
            case 0x49: *key = KEY_PAGEUP; return 1;
            case 0x51: *key = KEY_PAGEDOWN; return 1;
            default: return 0;
        }
    }

    if (scancode & 0x80) {
        u8 released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) shift_pressed = 0;
        if (released == 0x1D) ctrl_pressed = 0;
        return 0;
    }
    if (scancode == 0x1D) { ctrl_pressed = 1; return 0; }
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return 0;
    }
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return 0;
    }
    if (scancode >= 128) return 0;

    char c = keymap[scancode];
    if (!c) return 0;
    if (c >= 'a' && c <= 'z') {
        if (ctrl_pressed) c = (char)(c - 'a' + 1);
        else if (shift_pressed != caps_lock) c = (char)(c - 'a' + 'A');
    } else if (shift_pressed && shift_keymap[scancode]) {
        c = shift_keymap[scancode];
    }
    *key = (int)(u8)c;
    return 1;
}

int serial_try_getkey(int *key) {
    if (!key) return 0;

    /* Modifier and key-release bytes do not produce characters. Keep scanning
       a small bounded number so one poll can pass over them without blocking. */
    for (u32 budget = 0; budget < 32u; budget++) {
        u8 status = inb(0x64);
        if (!(status & 0x01u)) return 0;
        u8 scancode = inb(0x60);
        if (status & 0x20u) continue; /* Ignore auxiliary-device bytes; this text console has no pointer driver. */

        if (scancode == 0xE0) {
            keyboard_extended_pending = 1;
            continue;
        }
        int extended = keyboard_extended_pending;
        keyboard_extended_pending = 0;
        if (keyboard_decode_scancode(scancode, extended, key)) return 1;
    }
    return 0;
}

int serial_getkey(void) {
    int key;
    while (!serial_try_getkey(&key)) {
#ifndef DENOS_TEST_IO
        scheduler_run();
#endif
        __asm__ volatile("pause");
    }
    return key;
}

char serial_getc(void) {
    for (;;) {
        int key = serial_getkey();
        if (key < 256) return (char)key;
    }
}

/* ==========================================
   Read terminal line
   ========================================== */

void serial_readline(char *b, usize n) {
    usize i = 0;

    if (!n) {
        return;
    }

    for (;;) {
        char c = serial_getc();

        if (c == '\r' || c == '\n') {
            serial_putc('\n');
            break;
        }

        if ((c == 8 || c == 127) && i) {
            --i;
            serial_write("\b \b");
            continue;
        }

        if (c >= 32 && c < 127 && i + 1 < n) {
            b[i++] = c;
            serial_putc(c);
        }
    }

    b[i] = 0;
}

