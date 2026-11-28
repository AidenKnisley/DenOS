#include "terminal_apps.h"
#include "serial.h"
#include "terminal_ui.h"

void terminal_apps_list(void) {
    serial_write(TERM_CYAN "DENOS NATIVE TERMINAL APPS" TERM_RESET "\n\n");
    serial_write("  browse [PATH]   Keyboard file browser with bounded text preview\n");
    serial_write("  calc [EXPR]     Checked integer expression calculator\n");
    serial_write("  notes [FILE]    Open a note in the built-in editor\n");
    serial_write("  monitor         Live heap, memory, storage and task monitor\n");
    serial_write("  arcade          Snake, Breakout and Maze Runner\n");
    serial_write("  edit FILE       Full-screen native text editor\n\n");
    serial_write("Built-in apps run in the normal text console. No desktop or mouse is used.\n");
}
