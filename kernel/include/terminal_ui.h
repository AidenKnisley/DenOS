#ifndef DENOS_TERMINAL_UI_H
#define DENOS_TERMINAL_UI_H

#include "types.h"
#include "serial.h"
#include "kformat.h"

#define TERM_RESET "\033[0m"
#define TERM_CYAN  "\033[1;36m"
#define TERM_GREEN "\033[1;32m"
#define TERM_YELLOW "\033[1;33m"
#define TERM_RED   "\033[1;31m"
#define TERM_BLUE  "\033[1;34m"
#define TERM_DIM   "\033[90m"

static inline void terminal_ui_clear(void) {
    serial_write("\033[0m\033[2J\033[H");
}

static inline void terminal_ui_wait_key(const char *message) {
    serial_write("\n");
    serial_write(message ? message : "Press any key to continue...");
    (void)serial_getkey();
}

static inline void terminal_ui_u64(u64 value) {
    char text[32];
    (void)ksnprintf(text, sizeof(text), "%llu", (unsigned long long)value);
    serial_write(text);
}

#endif
