#ifndef AIDEN_SERIAL_H
#define AIDEN_SERIAL_H
#include "types.h"
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_set_mirror(int enabled);
void serial_cursor_enable(int enabled);
/* Raw keyboard events. Printable keys and Enter/Backspace use ASCII values. */
#define KEY_UP       0x100
#define KEY_DOWN     0x101
#define KEY_LEFT     0x102
#define KEY_RIGHT    0x103
#define KEY_HOME     0x104
#define KEY_END      0x105
#define KEY_DELETE   0x106
#define KEY_PAGEUP   0x107
#define KEY_PAGEDOWN 0x108
int serial_getkey(void);
char serial_getc(void);
void serial_readline(char *buf, usize cap);
#endif
