
#include "io.h"
#include "serial.h"

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
static int uart_mirror = 1;

static int shift_pressed = 0;
static int caps_lock = 0;
static int ctrl_pressed = 0;

/* ==========================================
   UART / Serial
   ========================================== */

static int tx_ready(void) {
    return inb(0x3FD) & 0x20;
}

static void uart_put_raw(char c) {
    while (!tx_ready()) {
        __asm__ volatile("pause");
    }

    outb(0x3F8, (u8)c);
}

void serial_init(void) {
    /* Initialize COM1 */

    uart_mirror = 1;
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);

    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);

    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);

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
    uart_mirror = enabled ? 1 : 0;
}

void serial_putc(char c) {
    if (uart_mirror) {
        if (c == '\n') {
            uart_put_raw('\r');
            uart_put_raw('\n');
        } else {
            uart_put_raw(c);
        }
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
   Read one PS/2 keyboard event (set-1 scancodes)
   ========================================== */

static u8 keyboard_read_scancode(void) {
    for (;;) {
        while (!(inb(0x64) & 1)) __asm__ volatile("pause");
        u8 status = inb(0x64);
        u8 code = inb(0x60);
        if (status & 0x20) continue; /* Ignore auxiliary/mouse data. */
        return code;
    }
}

int serial_getkey(void) {
    for (;;) {
        u8 scancode = keyboard_read_scancode();
        int extended = 0;
        if (scancode == 0xE0) {
            extended = 1;
            scancode = keyboard_read_scancode();
        }

        if (extended) {
            if (scancode & 0x80) {
                if ((scancode & 0x7F) == 0x1D) ctrl_pressed = 0;
                continue;
            }
            if (scancode == 0x1D) { ctrl_pressed = 1; continue; }
            switch (scancode) {
                case 0x48: return KEY_UP;
                case 0x50: return KEY_DOWN;
                case 0x4B: return KEY_LEFT;
                case 0x4D: return KEY_RIGHT;
                case 0x47: return KEY_HOME;
                case 0x4F: return KEY_END;
                case 0x53: return KEY_DELETE;
                case 0x49: return KEY_PAGEUP;
                case 0x51: return KEY_PAGEDOWN;
                default: continue;
            }
        }

        if (scancode & 0x80) {
            u8 released = scancode & 0x7F;
            if (released == 0x2A || released == 0x36) shift_pressed = 0;
            if (released == 0x1D) ctrl_pressed = 0;
            continue;
        }
        if (scancode == 0x1D) { ctrl_pressed = 1; continue; }
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
            continue;
        }
        if (scancode == 0x3A) {
            caps_lock = !caps_lock;
            continue;
        }
        if (scancode >= 128) continue;
        char c = keymap[scancode];
        if (!c) continue;
        if (c >= 'a' && c <= 'z') {
            if (ctrl_pressed) return (int)(c - 'a' + 1);
            if (shift_pressed != caps_lock) c = (char)(c - 'a' + 'A');
        } else if (shift_pressed && shift_keymap[scancode]) {
            c = shift_keymap[scancode];
        }
        return (int)(u8)c;
    }
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

