#include "terminal_apps.h"
#include "serial.h"
#include "fs.h"
#include "path.h"
#include "kformat.h"
#include "kstring.h"
#include "terminal_ui.h"

static int term_join_path(const char *directory, const char *leaf,
                          char *output, usize capacity) {
    int written;
    if (!directory || !leaf || !output || capacity == 0) return -1;
    if (!directory[0]) written = ksnprintf(output, capacity, "%s", leaf);
    else written = ksnprintf(output, capacity, "%s/%s", directory, leaf);
    return written < 0 || (usize)written >= capacity ? -1 : 0;
}

static void browse_preview(const char *path, const char *name) {
    const char *contents = fs_read(path);
    terminal_ui_clear();
    serial_write(TERM_CYAN "FILE PREVIEW" TERM_RESET "\n\n");
    serial_write(TERM_YELLOW);
    serial_write(name);
    serial_write(TERM_RESET "\n");
    serial_write(TERM_DIM "------------------------------------------------------------" TERM_RESET "\n");
    if (contents) {
        usize shown = 0;
        usize lines = 0;
        while (contents[shown] && shown < 1800u && lines < 20u) {
            char c = contents[shown++];
            if (c == '\t') serial_write("    ");
            else serial_putc(c);
            if (c == '\n') lines++;
        }
        if (shown == 0) serial_write(TERM_DIM "(empty file)\n" TERM_RESET);
        if (contents[shown])
            serial_write(TERM_YELLOW "\n[Preview shortened — use cat or edit for the complete file]\n" TERM_RESET);
    } else serial_write(TERM_RED "Unable to read this file." TERM_RESET "\n");
    terminal_ui_wait_key("\nPress any key to return to the file list...");
}

void terminal_browse_run(const char *start_path) {
    char current[FS_PATH_CAPACITY];
    int selected = 0;
    int scroll = 0;
    const int visible = 14;
    if (!start_path) start_path = "home/user";
    if (kstrlcpy(current, start_path, sizeof(current)) >= sizeof(current) ||
        !fs_is_directory(current)) {
        serial_write(TERM_RED "browse: directory not found\n" TERM_RESET);
        return;
    }

    for (;;) {
        usize total = fs_entry_count_in_dir(current);
        int key = 0;
        if (selected >= (int)total) selected = total ? (int)total - 1 : 0;
        if (scroll > selected) scroll = selected;
        if (selected >= scroll + visible) scroll = selected - visible + 1;

        terminal_ui_clear();
        serial_write(TERM_CYAN "DENOS FILE BROWSER" TERM_RESET "\n");
        serial_write("Path: /"); serial_write(current[0] ? current : ""); serial_write("\n");
        serial_write(TERM_DIM "------------------------------------------------------------" TERM_RESET "\n");
        serial_write("     TYPE   SIZE       NAME\n");
        serial_write(TERM_DIM "------------------------------------------------------------" TERM_RESET "\n");
        for (int row = 0; row < visible; ++row) {
            int index = scroll + row;
            char name[FS_PATH_CAPACITY];
            usize size = 0;
            int type = 0;
            char details[32];
            if (index >= (int)total) {
                serial_write("\n");
                continue;
            }
            if (fs_get_entry_in_dir(current, (usize)index, name, sizeof(name),
                                    &size, &type) < 0) continue;
            if (index == selected) serial_write(TERM_GREEN "> " TERM_RESET);
            else serial_write("  ");
            if (type == FS_ENTRY_DIRECTORY) {
                serial_write(TERM_BLUE "DIR    ");
                serial_write("     --   " TERM_RESET);
            } else {
                serial_write(TERM_CYAN "FILE   " TERM_RESET);
                (void)ksnprintf(details, sizeof(details), "%7llu  ",
                                (unsigned long long)size);
                serial_write(details);
            }
            serial_write(name);
            if (type == FS_ENTRY_DIRECTORY) serial_write("/");
            serial_write("\n");
        }
        serial_write(TERM_DIM "------------------------------------------------------------" TERM_RESET "\n");
        serial_write("Up/Down select  Enter open/preview  Backspace parent  Q quit\n");
        serial_write("Items: "); terminal_ui_u64(total); serial_write("   Selected: ");
        if (total) terminal_ui_u64((u64)selected + 1u); else serial_write("0");
        serial_write("\n");

        key = serial_getkey();
        if (key == 'q' || key == 'Q' || key == 27) break;
        if (key == KEY_UP && selected > 0) selected--;
        else if (key == KEY_DOWN && selected + 1 < (int)total) selected++;
        else if (key == KEY_PAGEUP) { selected -= visible; if (selected < 0) selected = 0; }
        else if (key == KEY_PAGEDOWN) {
            selected += visible;
            if (selected >= (int)total) selected = total ? (int)total - 1 : 0;
        } else if (key == 8 || key == 127) {
            char parent[FS_PATH_CAPACITY];
            if (fs_path_parent(current, parent, sizeof(parent)) == 0) {
                (void)kstrlcpy(current, parent, sizeof(current));
                selected = scroll = 0;
            }
        } else if (key == '\n' || key == '\r') {
            char name[FS_PATH_CAPACITY];
            char full[FS_PATH_CAPACITY];
            usize size = 0;
            int type = 0;
            if (total && fs_get_entry_in_dir(current, (usize)selected, name,
                                             sizeof(name), &size, &type) == 0 &&
                term_join_path(current, name, full, sizeof(full)) == 0) {
                if (type == FS_ENTRY_DIRECTORY) {
                    (void)kstrlcpy(current, full, sizeof(current));
                    selected = scroll = 0;
                } else browse_preview(full, name);
            }
        }
    }
    terminal_ui_clear();
}

