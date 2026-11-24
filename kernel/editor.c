#include "serial.h"
#include "fs.h"
#include "types.h"
#include "version.h"
#include "completion.h"
#include "cvm.h"

/* The editor keeps the familiar Vim keys but draws directly into VGA text mode. */
#define ED_CAP       16384
#define ED_ROWS      22
#define ED_COLS      80
#define ED_GUTTER    7
#define ED_TEXT_COLS (ED_COLS - ED_GUTTER)
#define ED_NAME_CAP  64
#define ED_CMD_CAP   128
#define ED_CLIP_CAP  256
#define ED_COMPLETION_ITEMS CCOMPLETE_MAX_ITEMS

#define MODE_NORMAL  0
#define MODE_INSERT  1
#define MODE_COMMAND 2

static char ed_buffer[ED_CAP];
static char ed_undo[ED_CAP];
static char ed_redo[ED_CAP];
static char ed_clipboard[ED_CLIP_CAP];
static char ed_filename[ED_NAME_CAP];
static char ed_command[ED_CMD_CAP];
static char ed_message[ED_CMD_CAP];
static char ed_search[ED_CMD_CAP];
static usize ed_length;
static usize ed_cursor;
static usize ed_top_line;
static usize ed_undo_length;
static usize ed_undo_cursor;
static usize ed_redo_length;
static usize ed_redo_cursor;
static int ed_undo_valid;
static int ed_redo_valid;
static int ed_mode;
static int ed_dirty;
static int ed_show_numbers;
static int ed_block_comment;
static int ed_search_reverse;
static int ed_insert_checkpoint;
static int ed_completion_enabled;
static int ed_completion_active;
static int ed_completion_count;
static int ed_completion_selected;
static int ed_completion_dismissed;
static char ed_completion_prefix[CCOMPLETE_WORD_CAP];
static char ed_completion_dismissed_prefix[CCOMPLETE_WORD_CAP];
static CCompletionItem ed_completion_items[ED_COMPLETION_ITEMS];
/* Static storage avoids consuming the small kernel stack during compilation. */
static char ed_build_image[CVM_IMAGE_CAP];

static usize str_len(const char *s) {
    usize n = 0;
    while (s && s[n]) n++;
    return n;
}

static int str_eq(const char *a, const char *b) {
    usize i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static int str_starts(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static void str_copy(char *dst, const char *src, usize cap) {
    usize i = 0;
    if (!cap) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_ident(char c) { return is_alpha(c) || is_digit(c); }
static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static void set_message(const char *s) { str_copy(ed_message, s, sizeof(ed_message)); }

static void print_spaces(usize count) {
    while (count--) serial_putc(' ');
}

static void write_limited(const char *s, usize limit) {
    usize n = 0;
    while (s && *s && n < limit) {
        serial_putc(*s++);
        n++;
    }
}

static void print_u32(usize value) {
    char digits[16];
    int n = 0;
    if (!value) { serial_putc('0'); return; }
    while (value && n < 15) {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (n) serial_putc(digits[--n]);
}

static usize decimal_to_buffer(char *out, usize value) {
    char digits[16];
    usize n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && n < sizeof(digits));
    for (usize i = 0; i < n; i++) out[i] = digits[n - i - 1];
    return n;
}

static usize line_start(usize pos) {
    if (pos > ed_length) pos = ed_length;
    while (pos && ed_buffer[pos - 1] != '\n') pos--;
    return pos;
}

static usize line_end(usize pos) {
    if (pos > ed_length) pos = ed_length;
    while (pos < ed_length && ed_buffer[pos] != '\n') pos++;
    return pos;
}

static usize line_number_at(usize pos) {
    usize line = 0;
    if (pos > ed_length) pos = ed_length;
    for (usize i = 0; i < pos; i++) if (ed_buffer[i] == '\n') line++;
    return line;
}

static usize line_start_number(usize number) {
    usize pos = 0;
    while (number && pos < ed_length) {
        if (ed_buffer[pos++] == '\n') number--;
    }
    return pos;
}

static usize total_lines(void) {
    usize n = 1;
    for (usize i = 0; i < ed_length; i++) if (ed_buffer[i] == '\n') n++;
    return n;
}

static usize column_at(usize pos) {
    usize start = line_start(pos);
    return pos - start;
}

static usize position_for_column(usize start, usize col) {
    usize end = line_end(start);
    usize available = end - start;
    if (col > available) col = available;
    return start + col;
}

static void checkpoint(void) {
    for (usize i = 0; i <= ed_length; i++) ed_undo[i] = ed_buffer[i];
    ed_undo_length = ed_length;
    ed_undo_cursor = ed_cursor;
    ed_undo_valid = 1;
    ed_redo_valid = 0;
}

static int insert_at(usize pos, char c) {
    if (ed_length + 1 >= ED_CAP) {
        set_message("Buffer full: cannot insert more text");
        return 0;
    }
    if (pos > ed_length) pos = ed_length;
    for (usize i = ed_length + 1; i > pos; i--) ed_buffer[i] = ed_buffer[i - 1];
    ed_buffer[pos] = c;
    ed_length++;
    ed_cursor = pos + 1;
    ed_dirty = 1;
    return 1;
}

static void delete_range(usize first, usize last) {
    if (first > ed_length) first = ed_length;
    if (last > ed_length) last = ed_length;
    if (last <= first) return;
    usize count = last - first;
    for (usize i = first; i + count <= ed_length; i++) ed_buffer[i] = ed_buffer[i + count];
    ed_length -= count;
    ed_buffer[ed_length] = 0;
    ed_cursor = first > ed_length ? ed_length : first;
    ed_dirty = 1;
}

static void ensure_cursor_visible(void) {
    usize line = line_number_at(ed_cursor);
    if (line < ed_top_line) ed_top_line = line;
    else if (line >= ed_top_line + ED_ROWS) ed_top_line = line - ED_ROWS + 1;
}

static int token_is(const char *token, const char *list) {
    usize n = str_len(token);
    const char *p = list;
    while (*p) {
        while (*p == ' ') p++;
        usize start = 0;
        while (p[start] && p[start] != ' ') start++;
        if (start == n) {
            usize i = 0;
            while (i < n && token[i] == p[i]) i++;
            if (i == n) return 1;
        }
        p += start;
    }
    return 0;
}

static void syntax_line(usize start, usize end, int *block_comment) {
    static const char *keywords =
        "auto break case const continue default do else enum extern for goto if inline "
        "register restrict return sizeof static struct switch typedef union volatile while";
    static const char *types =
        "void char short int long float double signed unsigned _Bool size_t u8 u16 u32 u64 i32 i64";
    usize p = start;
    int first_nonspace = 1;
    while (p < end && is_space(ed_buffer[p])) p++;
    int preprocessor = p < end && ed_buffer[p] == '#';
    p = start;

    while (p < end && p - start < ED_TEXT_COLS) {
        char c = ed_buffer[p];
        if (preprocessor) {
            serial_write("\033[95m");
            while (p < end && p - start < ED_TEXT_COLS) serial_putc(ed_buffer[p++]);
            serial_write("\033[37m");
            return;
        }
        if (*block_comment) {
            serial_write("\033[32m");
            if (c == '*' && p + 1 < end && ed_buffer[p + 1] == '/') {
                serial_putc('*'); serial_putc('/'); p += 2;
                *block_comment = 0;
                serial_write("\033[37m");
            } else {
                serial_putc(c); p++;
            }
            continue;
        }
        if (c == '/' && p + 1 < end && ed_buffer[p + 1] == '/') {
            serial_write("\033[32m");
            while (p < end && p - start < ED_TEXT_COLS) serial_putc(ed_buffer[p++]);
            serial_write("\033[37m");
            return;
        }
        if (c == '/' && p + 1 < end && ed_buffer[p + 1] == '*') {
            serial_write("\033[32m");
            serial_putc('/'); serial_putc('*'); p += 2;
            *block_comment = 1;
            continue;
        }
        if (c == '"' || c == '\'') {
            char quote = c;
            serial_write("\033[92m");
            serial_putc(ed_buffer[p++]);
            while (p < end && p - start < ED_TEXT_COLS) {
                char q = ed_buffer[p++];
                serial_putc(q);
                if (q == '\\' && p < end) serial_putc(ed_buffer[p++]);
                else if (q == quote) break;
            }
            serial_write("\033[37m");
            continue;
        }
        if (is_alpha(c)) {
            char token[64];
            usize n = 0;
            usize begin = p;
            while (p < end && is_ident(ed_buffer[p])) {
                if (n + 1 < sizeof(token)) token[n++] = ed_buffer[p];
                p++;
            }
            token[n] = 0;
            usize look = p;
            while (look < end && is_space(ed_buffer[look])) look++;
            if (token_is(token, keywords)) serial_write("\033[95m");
            else if (token_is(token, types)) serial_write("\033[96m");
            else if (look < end && ed_buffer[look] == '(') serial_write("\033[94m");
            for (usize i = begin; i < p; i++) serial_putc(ed_buffer[i]);
            serial_write("\033[37m");
            continue;
        }
        if (is_digit(c)) {
            serial_write("\033[93m");
            while (p < end && (is_ident(ed_buffer[p]) || ed_buffer[p] == '.'))
                serial_putc(ed_buffer[p++]);
            serial_write("\033[37m");
            continue;
        }
        if (c == '\t') {
            usize col = p - start;
            usize spaces = 4 - (col % 4);
            while (spaces--) serial_putc(' ');
        } else {
            serial_putc(c);
        }
        p++;
        first_nonspace = 0;
    }
    (void)first_nonspace;
}

static int is_c_source_file(void) {
    usize n = str_len(ed_filename);
    if (n < 2) return 0;
    const char *dot = ed_filename + n - 2;
    if (dot[0] == '.' && (dot[1] == 'c' || dot[1] == 'h')) return 1;
    return 0;
}

static void completion_clear(void) {
    ed_completion_active = 0;
    ed_completion_count = 0;
    ed_completion_selected = 0;
    ed_completion_prefix[0] = 0;
}

static void completion_refresh(void) {
    char prefix[CCOMPLETE_WORD_CAP];
    usize begin = ed_cursor;
    usize n;
    if (!ed_completion_enabled || ed_mode != MODE_INSERT || !is_c_source_file()) {
        completion_clear();
        return;
    }
    if (begin > ed_length) begin = ed_length;
    while (begin && is_ident(ed_buffer[begin - 1])) begin--;
    n = ed_cursor - begin;
    if (n < 2 || n >= sizeof(prefix)) {
        ed_completion_dismissed = 0;
        ed_completion_dismissed_prefix[0] = 0;
        completion_clear();
        return;
    }
    for (usize i = 0; i < n; ++i) prefix[i] = ed_buffer[begin + i];
    prefix[n] = 0;
    if (ed_completion_dismissed) {
        if (str_eq(prefix, ed_completion_dismissed_prefix)) {
            completion_clear();
            return;
        }
        ed_completion_dismissed = 0;
        ed_completion_dismissed_prefix[0] = 0;
    }
    if (!str_eq(prefix, ed_completion_prefix)) ed_completion_selected = 0;
    str_copy(ed_completion_prefix, prefix, sizeof(ed_completion_prefix));
    ed_completion_count = c_completion_collect(ed_buffer, ed_length, prefix,
                                                ed_completion_items, ED_COMPLETION_ITEMS);
    if (ed_completion_count <= 0) {
        completion_clear();
        return;
    }
    if (ed_completion_selected >= ed_completion_count) ed_completion_selected = 0;
    ed_completion_active = 1;
}

static void completion_cycle(int direction) {
    if (!ed_completion_active || !ed_completion_count) {
        set_message("No code suggestions for this prefix");
        return;
    }
    if (direction > 0)
        ed_completion_selected = (ed_completion_selected + 1) % ed_completion_count;
    else
        ed_completion_selected = (ed_completion_selected + ed_completion_count - 1) % ed_completion_count;
    set_message("Suggestion selected; Tab inserts, Ctrl-N/P cycles");
}

static void completion_accept(void) {
    if (!ed_completion_active || ed_completion_selected >= ed_completion_count) return;
    CCompletionItem *item = &ed_completion_items[ed_completion_selected];
    usize prefix_length = str_len(ed_completion_prefix);
    usize item_length = str_len(item->text);
    if (item_length <= prefix_length) { completion_clear(); return; }
    if (!ed_insert_checkpoint) { checkpoint(); ed_insert_checkpoint = 1; }
    for (usize i = prefix_length; i < item_length; ++i) {
        if (!insert_at(ed_cursor, item->text[i])) break;
    }
    if (item->kind == CCOMPLETE_FUNCTION &&
        (ed_cursor >= ed_length || ed_buffer[ed_cursor] != '(')) {
        if (insert_at(ed_cursor, '(') && insert_at(ed_cursor, ')') && ed_cursor) ed_cursor--;
    }
    set_message("Completed code identifier");
    completion_clear();
}

static const char *completion_kind_name(u8 kind) {
    if (kind == CCOMPLETE_FUNCTION) return "function";
    if (kind == CCOMPLETE_TYPE) return "type";
    if (kind == CCOMPLETE_KEYWORD) return "keyword";
    return "symbol";
}

static char completion_kind_icon(u8 kind) {
    if (kind == CCOMPLETE_FUNCTION) return 'f';
    if (kind == CCOMPLETE_TYPE) return 'T';
    if (kind == CCOMPLETE_KEYWORD) return 'K';
    return 'v';
}

static void terminal_move_to(usize row, usize column) {
    char command[24];
    usize length = 0;
    command[length++] = 27;
    command[length++] = '[';
    length += decimal_to_buffer(command + length, row);
    command[length++] = ';';
    length += decimal_to_buffer(command + length, column);
    command[length++] = 'H';
    command[length] = 0;
    serial_write(command);
}

/* Render an IntelliSense-style floating list over the editing area. The
 * underlying file is redrawn on every frame, so the popup never damages text. */
static void completion_popup_render(usize cursor_row, usize cursor_column) {
    enum { POPUP_WIDTH = 40, POPUP_VISIBLE = 5 };
    char border[POPUP_WIDTH + 1];
    int visible;
    int first;
    usize top;
    usize left;
    usize height;

    if (!ed_completion_active || !ed_completion_count) return;
    visible = ed_completion_count < POPUP_VISIBLE
            ? ed_completion_count : POPUP_VISIBLE;
    first = ed_completion_selected - visible / 2;
    if (first < 0) first = 0;
    if (first + visible > ed_completion_count)
        first = ed_completion_count - visible;

    height = (usize)visible + 2u;
    top = cursor_row + 1u;
    if (top + height - 1u > ED_ROWS + 1u)
        top = cursor_row > height ? cursor_row - height + 1u : 2u;
    if (top < 2u) top = 2u;
    if (top + height - 1u > ED_ROWS + 1u)
        top = (ED_ROWS + 2u) - height;

    left = cursor_column;
    if (left + POPUP_WIDTH - 1u > ED_COLS)
        left = ED_COLS - POPUP_WIDTH + 1u;
    if (left < 1u) left = 1u;

    border[0] = '+';
    for (int i = 1; i < POPUP_WIDTH - 1; ++i) border[i] = '-';
    border[POPUP_WIDTH - 1] = '+';
    border[POPUP_WIDTH] = 0;

    terminal_move_to(top, left);
    serial_write("\033[96;40m");
    serial_write(border);
    serial_write("\033[0m");

    for (int row = 0; row < visible; ++row) {
        int index = first + row;
        char line[POPUP_WIDTH + 1];
        char detail[12];
        usize detail_length;
        usize name_length;
        usize limit;
        int selected = index == ed_completion_selected;

        for (int i = 0; i < POPUP_WIDTH; ++i) line[i] = ' ';
        line[0] = '|';
        line[POPUP_WIDTH - 1] = '|';
        line[POPUP_WIDTH] = 0;
        line[2] = completion_kind_icon(ed_completion_items[index].kind);
        line[3] = ' ';
        line[1] = selected ? '>' : ' ';

        name_length = str_len(ed_completion_items[index].text);
        limit = name_length < 20u ? name_length : 20u;
        for (usize i = 0; i < limit; ++i) line[4u + i] = ed_completion_items[index].text[i];

        str_copy(detail, completion_kind_name(ed_completion_items[index].kind), sizeof(detail));
        detail_length = str_len(detail);
        if (detail_length > 9u) detail_length = 9u;
        for (usize i = 0; i < detail_length; ++i)
            line[POPUP_WIDTH - 2u - detail_length + i] = detail[i];

        terminal_move_to(top + 1u + (usize)row, left);
        serial_write(selected ? "\033[44;97m" : "\033[40;37m");
        serial_write(line);
        serial_write("\033[0m");
    }

    terminal_move_to(top + height - 1u, left);
    serial_write("\033[96;40m");
    serial_write(border);
    serial_write("\033[0m");
}

static void completion_render(void) {
    if (!ed_completion_active || !ed_completion_count) {
        serial_write(ed_message);
        return;
    }
    serial_write("\033[96m IntelliSense  ");
    print_u32((usize)(ed_completion_selected + 1));
    serial_putc('/');
    print_u32((usize)ed_completion_count);
    serial_write("  ");
    serial_write("\033[90m↑/↓ select  Tab accept  Esc dismiss\033[0m");
}

static void render(void) {
    ensure_cursor_visible();
    serial_write("\033[0m\033[2J\033[H");
    serial_write("\033[44;97m DENOS EDIT " DENOS_VERSION " | ");
    usize title_name_len = str_len(ed_filename);
    if (title_name_len > 54) title_name_len = 54;
    write_limited(ed_filename, title_name_len);
    if (ed_dirty) serial_write(" [+]");
    usize title_prefix_len = sizeof(" DENOS EDIT " DENOS_VERSION " | ") - 1;
    usize title_len = title_prefix_len + title_name_len + (ed_dirty ? 4 : 0);
    if (title_len < ED_COLS) print_spaces(ED_COLS - title_len);
    serial_write("\033[0m");
    serial_putc('\n');

    usize start = line_start_number(ed_top_line);
    int block_comment = 0;
    for (usize row = 0; row < ED_ROWS; row++) {
        if (start <= ed_length) {
            usize end = line_end(start);
            int current_line = (ed_top_line + row == line_number_at(ed_cursor));
            if (current_line) serial_write("\033[96m>\033[0m");
            else serial_putc(' ');
            if (ed_show_numbers) {
                serial_write(current_line ? "\033[1;96m" : "\033[90m");
                if (ed_top_line + row + 1 < 10) serial_putc(' ');
                if (ed_top_line + row + 1 < 100) serial_putc(' ');
                if (ed_top_line + row + 1 < 1000) serial_putc(' ');
                print_u32(ed_top_line + row + 1);
                serial_write(" \033[90m|");
                serial_write("\033[37m");
            } else {
                serial_write("      ");
            }
            /* The blue row makes the active line easy to follow in a full-screen edit. */
            if (current_line) serial_write("\033[44m");
            syntax_line(start, end, &block_comment);
            serial_write("\033[K\033[0m\n");
            start = end < ed_length ? end + 1 : ed_length + 1;
        } else {
            serial_write("      \033[K\n");
        }
    }

    serial_write("\033[44;97m");
    if (ed_mode == MODE_INSERT) serial_write(" -- INSERT -- ");
    else if (ed_mode == MODE_COMMAND) serial_write(" -- COMMAND -- ");
    else serial_write(" NORMAL ");
    write_limited(ed_filename, 28);
    if (ed_dirty) serial_write(" [+]");
    serial_write("  Ln ");
    print_u32(line_number_at(ed_cursor) + 1);
    serial_write(", Col ");
    print_u32(column_at(ed_cursor) + 1);
    serial_write("  ");
    serial_write("\033[K\033[0m\n");

    if (ed_mode == MODE_COMMAND) {
        serial_putc(':');
        serial_write(ed_command);
    } else {
        completion_render();
    }
    serial_write("\033[K");

    usize line = line_number_at(ed_cursor);
    usize col = column_at(ed_cursor);
    usize screen_row = line >= ed_top_line ? line - ed_top_line : 0;
    if (screen_row >= ED_ROWS) screen_row = ED_ROWS - 1;
    usize screen_col = ED_GUTTER + col;
    if (screen_col >= ED_COLS) screen_col = ED_COLS - 1;
    if (ed_mode == MODE_COMMAND) {
        screen_row = ED_ROWS + 1;
        screen_col = 1 + str_len(ed_command);
        if (screen_col >= ED_COLS) screen_col = ED_COLS - 1;
    }
    if (ed_mode == MODE_INSERT && ed_completion_active)
        completion_popup_render(screen_row + 2u, screen_col + 1u);

    char cursor_command[24];
    usize cursor_length = 0;
    cursor_command[cursor_length++] = 27;
    cursor_command[cursor_length++] = '[';
    cursor_length += decimal_to_buffer(cursor_command + cursor_length, screen_row + 2);
    cursor_command[cursor_length++] = ';';
    cursor_length += decimal_to_buffer(cursor_command + cursor_length, screen_col + 1);
    cursor_command[cursor_length++] = 'H';
    cursor_command[cursor_length] = 0;
    serial_write(cursor_command);
}

static void insert_newline_with_indent(void) {
    char indent_text[64];
    usize start = line_start(ed_cursor);
    usize indent = 0;
    while (start + indent < ed_length &&
           (ed_buffer[start + indent] == ' ' || ed_buffer[start + indent] == '\t') &&
           indent + 1 < sizeof(indent_text)) {
        indent_text[indent] = ed_buffer[start + indent];
        indent++;
    }

    usize previous = ed_cursor;
    while (previous > start && is_space(ed_buffer[previous - 1])) previous--;
    int add_block_indent = previous > start && ed_buffer[previous - 1] == '{';

    usize next = ed_cursor;
    while (next < ed_length && is_space(ed_buffer[next])) next++;
    usize remove_indent = 0;
    if (next < ed_length && ed_buffer[next] == '}') {
        if (indent >= 4) remove_indent = 4;
        else if (indent && indent_text[indent - 1] == '\t') remove_indent = 1;
    }

    if (!ed_insert_checkpoint) {
        checkpoint();
        ed_insert_checkpoint = 1;
    }
    if (!insert_at(ed_cursor, '\n')) return;
    for (usize i = 0; i < indent - remove_indent; i++)
        insert_at(ed_cursor, indent_text[i]);
    if (add_block_indent) {
        for (int i = 0; i < 4; i++) insert_at(ed_cursor, ' ');
    }
}

static void move_vertical(int down) {
    usize col = column_at(ed_cursor);
    usize line = line_number_at(ed_cursor);
    usize lines = total_lines();
    if (down) { if (line + 1 < lines) line++; }
    else if (line) line--;
    ed_cursor = position_for_column(line_start_number(line), col);
    if (ed_mode == MODE_NORMAL && ed_cursor == line_end(ed_cursor) && ed_cursor > line_start(ed_cursor)) ed_cursor--;
}

static void move_word_forward(void) {
    if (ed_cursor < ed_length && is_ident(ed_buffer[ed_cursor]))
        while (ed_cursor < ed_length && is_ident(ed_buffer[ed_cursor])) ed_cursor++;
    while (ed_cursor < ed_length && !is_ident(ed_buffer[ed_cursor])) ed_cursor++;
    if (ed_cursor > ed_length) ed_cursor = ed_length;
    if (ed_mode == MODE_NORMAL && ed_cursor == ed_length && ed_cursor) ed_cursor--;
}

static void move_word_backward(void) {
    if (ed_cursor) ed_cursor--;
    while (ed_cursor && !is_ident(ed_buffer[ed_cursor])) ed_cursor--;
    while (ed_cursor && is_ident(ed_buffer[ed_cursor - 1])) ed_cursor--;
}

static void save_file(void) {
    if (fs_write(ed_filename, ed_buffer) == 0) {
        ed_dirty = 0;
        if (fs_is_persistent()) set_message("\033[92m\" file saved to disk \033[0m");
        else set_message("Saved to RAM only; changes will be lost after reboot");
    } else {
        set_message(fs_last_error_message());
    }
}

static int find_text(const char *needle, int reverse, usize from, usize *found) {
    usize n = str_len(needle);
    if (!n || n > ed_length) return 0;
    if (!reverse) {
        for (usize i = from; i + n <= ed_length; i++) {
            usize j = 0;
            while (j < n && ed_buffer[i + j] == needle[j]) j++;
            if (j == n) { *found = i; return 1; }
        }
        for (usize i = 0; i < from && i + n <= ed_length; i++) {
            usize j = 0;
            while (j < n && ed_buffer[i + j] == needle[j]) j++;
            if (j == n) { *found = i; return 1; }
        }
    } else {
        usize i = from < ed_length ? from : ed_length;
        for (;;) {
            if (i + n <= ed_length) {
                usize j = 0;
                while (j < n && ed_buffer[i + j] == needle[j]) j++;
                if (j == n) { *found = i; return 1; }
            }
            if (!i) break;
            i--;
        }
    }
    return 0;
}

static void do_search(int reverse) {
    usize found;
    if (!ed_search[0]) { set_message("No previous search pattern"); return; }
    if (find_text(ed_search, reverse, reverse ? (ed_cursor ? ed_cursor - 1 : 0) : ed_cursor + 1, &found)) {
        ed_cursor = found;
        ed_search_reverse = reverse;
        set_message("Search match");
    } else set_message("Pattern not found");
}

static void editor_check_structure(void) {
    struct delimiter { char value; usize line; } stack[128];
    usize depth = 0, line = 1;
    int in_block = 0, in_line = 0;
    char quote = 0;
    int escaped = 0;
    for (usize i = 0; i < ed_length; ++i) {
        char c = ed_buffer[i];
        char next = i + 1 < ed_length ? ed_buffer[i + 1] : 0;
        if (c == '\n') {
            line++;
            in_line = 0;
            if (quote) {
                set_message("Structural check: newline inside an unterminated string/character literal");
                return;
            }
            continue;
        }
        if (in_line) continue;
        if (in_block) {
            if (c == '*' && next == '/') { in_block = 0; i++; }
            continue;
        }
        if (quote) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '/' && next == '/') { in_line = 1; i++; continue; }
        if (c == '/' && next == '*') { in_block = 1; i++; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '(' || c == '[' || c == '{') {
            if (depth >= sizeof(stack) / sizeof(stack[0])) {
                set_message("Structural check: nesting exceeds 128 delimiters");
                return;
            }
            stack[depth].value = c;
            stack[depth].line = line;
            depth++;
        } else if (c == ')' || c == ']' || c == '}') {
            char expected = c == ')' ? '(' : c == ']' ? '[' : '{';
            if (!depth || stack[depth - 1].value != expected) {
                set_message("Structural check: mismatched closing delimiter");
                return;
            }
            depth--;
        }
    }
    if (quote) { set_message("Structural check: unterminated string/character literal"); return; }
    if (in_block) { set_message("Structural check: unterminated block comment"); return; }
    if (depth) { set_message("Structural check: unmatched opening delimiter"); return; }
    set_message("Structural check passed: delimiters, comments and literals are balanced (not a full compiler check)");
}

static void command_execute(void) {
    char *cmd = ed_command;
    while (*cmd == ' ') cmd++;
    if (str_eq(cmd, "w") || str_starts(cmd, "w ")) {
        if (cmd[1] == ' ') {
            char *name = cmd + 2;
            if (str_len(name) && str_len(name) < ED_NAME_CAP) {
                str_copy(ed_filename, name, sizeof(ed_filename));
            }
        }
        save_file();
    } else if (str_eq(cmd, "q")) {
        if (ed_dirty) set_message("No write since last change (use :q! to discard)");
        else ed_mode = -1;
    } else if (str_eq(cmd, "q!") || str_eq(cmd, "quit!")) {
        ed_mode = -1;
    } else if (str_eq(cmd, "wq") || str_eq(cmd, "x")) {
        save_file();
        if (!ed_dirty) ed_mode = -1;
    } else if (str_eq(cmd, "set number") || str_eq(cmd, "set nu")) {
        ed_show_numbers = 1; set_message("Line numbers enabled");
    } else if (str_eq(cmd, "set nonumber") || str_eq(cmd, "set nonu")) {
        ed_show_numbers = 0; set_message("Line numbers disabled");
    } else if (str_eq(cmd, "set complete")) {
        ed_completion_enabled = 1; completion_refresh(); set_message("C code completion enabled");
    } else if (str_eq(cmd, "set nocomplete")) {
        ed_completion_enabled = 0; completion_clear(); set_message("C code completion disabled");
    } else if (str_eq(cmd, "check")) {
        editor_check_structure();
    } else if (str_eq(cmd, "make") || str_eq(cmd, "compile")) {
        if (!ed_filename[0]) {
            set_message("Compile: save the buffer with a filename first");
        } else {
            int image_size = c_compile(ed_buffer, ed_build_image, sizeof(ed_build_image));
            if (image_size < 0) {
                str_copy(ed_message, c_compile_error(), sizeof(ed_message));
            } else {
                char result[ED_CMD_CAP];
                /* Avoid a formatting dependency in the editor command path. */
                usize pos = 0;
                const char prefix[] = "Aiden C compile passed: ";
                while (prefix[pos] && pos + 1 < sizeof(result)) { result[pos] = prefix[pos]; pos++; }
                char digits[16];
                usize dn = 0;
                unsigned value = (unsigned)image_size;
                do { digits[dn++] = (char)('0' + value % 10u); value /= 10u; } while (value && dn < sizeof(digits));
                while (dn && pos + 1 < sizeof(result)) result[pos++] = digits[--dn];
                const char suffix[] = " byte AEX image";
                for (usize j = 0; suffix[j] && pos + 1 < sizeof(result); ++j) result[pos++] = suffix[j];
                result[pos] = 0;
                set_message(result);
            }
        }
    } else if (str_eq(cmd, "help")) {
        set_message("i/a/o insert  :w save  :q quit  :check  :make compile  suggestions: Ctrl-N/P, Tab");
    } else if (str_starts(cmd, "e ")) {
        const char *data = fs_read(cmd + 2);
        if (ed_dirty) set_message("Unsaved changes: save or quit with :q! first");
        else if (!data) set_message("File not found");
        else {
            usize i = 0;
            while (data[i] && i + 1 < ED_CAP) { ed_buffer[i] = data[i]; i++; }
            ed_buffer[i] = 0; ed_length = i; ed_cursor = 0;
            str_copy(ed_filename, cmd + 2, sizeof(ed_filename));
            ed_dirty = 0; ed_top_line = 0; set_message("File opened");
        }
    } else if (*cmd == '/' || *cmd == '?') {
        int reverse = *cmd == '?';
        str_copy(ed_search, cmd + 1, sizeof(ed_search));
        do_search(reverse);
    } else if (!*cmd) {
        /* Empty Ex command is ignored. */
    } else {
        set_message("Not an editor command (try :help)");
    }
    ed_mode = ed_mode == -1 ? -1 : MODE_NORMAL;
}

static void command_key(int key) {
    usize n = str_len(ed_command);
    if (key == '\n' || key == '\r') {
        command_execute();
        ed_command[0] = 0;
        return;
    }
    if (key == 27) {
        ed_mode = MODE_NORMAL; ed_command[0] = 0; set_message(""); return;
    }
    if (key == '\b' || key == 127) {
        if (n) ed_command[n - 1] = 0;
        return;
    }
    if (key >= 32 && key < 127 && n + 1 < sizeof(ed_command)) {
        ed_command[n] = (char)key;
        ed_command[n + 1] = 0;
    }
}

static void insert_mode_key(int key) {
    if (key == 14) { completion_refresh(); completion_cycle(1); return; }  /* Ctrl-N */
    if (key == 16) { completion_refresh(); completion_cycle(-1); return; } /* Ctrl-P */
    if (ed_completion_active && key == KEY_DOWN) { completion_cycle(1); return; }
    if (ed_completion_active && key == KEY_UP) { completion_cycle(-1); return; }
    if (key == '\t' && ed_completion_active) { completion_accept(); return; }
    if (key == 27 && ed_completion_active) {
        str_copy(ed_completion_dismissed_prefix, ed_completion_prefix,
                 sizeof(ed_completion_dismissed_prefix));
        ed_completion_dismissed = 1;
        completion_clear();
        set_message("");
        return;
    }
    if (key == 27) {
        completion_clear();
        ed_completion_dismissed = 0;
        ed_completion_dismissed_prefix[0] = 0;
        ed_mode = MODE_NORMAL;
        ed_insert_checkpoint = 0;
        if (ed_cursor) ed_cursor--;
        set_message("");
        return;
    }
    if (key == KEY_LEFT) { if (ed_cursor) ed_cursor--; return; }
    if (key == KEY_RIGHT) { if (ed_cursor < ed_length) ed_cursor++; return; }
    if (key == KEY_UP) { move_vertical(0); return; }
    if (key == KEY_DOWN) { move_vertical(1); return; }
    if (key == KEY_PAGEUP) { for (int i = 0; i < ED_ROWS - 1; i++) move_vertical(0); return; }
    if (key == KEY_PAGEDOWN) { for (int i = 0; i < ED_ROWS - 1; i++) move_vertical(1); return; }
    if (key == KEY_HOME) { ed_cursor = line_start(ed_cursor); return; }
    if (key == KEY_END) { ed_cursor = line_end(ed_cursor); return; }
    if (key == KEY_DELETE) {
        if (ed_cursor < ed_length) {
            if (!ed_insert_checkpoint) { checkpoint(); ed_insert_checkpoint = 1; }
            delete_range(ed_cursor, ed_cursor + 1);
        }
        return;
    }
    if (key == '\b' || key == 127) {
        if (ed_cursor) {
            if (!ed_insert_checkpoint) { checkpoint(); ed_insert_checkpoint = 1; }
            delete_range(ed_cursor - 1, ed_cursor);
        }
        return;
    }
    if (key == '\n' || key == '\r') {
        insert_newline_with_indent();
        return;
    }
    if (key == '}') {
        usize start = line_start(ed_cursor);
        usize end = line_end(ed_cursor);
        usize indent = 0;
        while (start + indent < end && is_space(ed_buffer[start + indent])) indent++;
        if (ed_cursor == start + indent && indent) {
            if (!ed_insert_checkpoint) { checkpoint(); ed_insert_checkpoint = 1; }
            usize remove = indent >= 4 ? 4 :
                           (ed_buffer[start] == '\t' ? 1 : 0);
            if (remove) delete_range(ed_cursor - remove, ed_cursor);
        }
        if (!ed_insert_checkpoint) { checkpoint(); ed_insert_checkpoint = 1; }
        insert_at(ed_cursor, '}');
        return;
    }
    if (key == '\t' || (key >= 32 && key < 127)) {
        if (!ed_insert_checkpoint) { checkpoint(); ed_insert_checkpoint = 1; }
        insert_at(ed_cursor, (char)key);
    }
}

static void normal_mode_key(int key, int *pending) {
    usize start = line_start(ed_cursor);
    usize end = line_end(ed_cursor);
    if (key == KEY_UP || key == 'k') { move_vertical(0); return; }
    if (key == KEY_DOWN || key == 'j') { move_vertical(1); return; }
    if (key == KEY_PAGEUP) { for (int i = 0; i < ED_ROWS - 1; i++) move_vertical(0); return; }
    if (key == KEY_PAGEDOWN) { for (int i = 0; i < ED_ROWS - 1; i++) move_vertical(1); return; }
    if (key == KEY_LEFT || key == 'h') { if (ed_cursor > start) ed_cursor--; return; }
    if (key == KEY_RIGHT || key == 'l') { if (ed_cursor + 1 < end) ed_cursor++; return; }
    if (key == KEY_HOME || key == '0') { ed_cursor = start; return; }
    if (key == KEY_END || key == '$') { if (end > start) ed_cursor = end - 1; else ed_cursor = start; return; }
    if (key == 'i') { ed_mode = MODE_INSERT; ed_insert_checkpoint = 0; set_message(""); return; }
    if (key == 'a') { if (ed_cursor < end) ed_cursor++; ed_mode = MODE_INSERT; ed_insert_checkpoint = 0; return; }
    if (key == 'I') { ed_cursor = start; while (ed_cursor < end && is_space(ed_buffer[ed_cursor])) ed_cursor++; ed_mode = MODE_INSERT; ed_insert_checkpoint = 0; return; }
    if (key == 'A') { ed_cursor = end; ed_mode = MODE_INSERT; ed_insert_checkpoint = 0; return; }
    if (key == 'o' || key == 'O') {
        checkpoint();
        if (key == 'o') {
            ed_cursor = end;
            if (ed_cursor < ed_length && ed_buffer[ed_cursor] == '\n') ed_cursor++;
        } else ed_cursor = start;
        insert_at(ed_cursor, '\n');
        ed_mode = MODE_INSERT; ed_insert_checkpoint = 1; return;
    }
    if (key == 'x' || key == KEY_DELETE) {
        if (ed_cursor < end) { checkpoint(); delete_range(ed_cursor, ed_cursor + 1); }
        return;
    }
    if (key == 'D') {
        if (ed_cursor < end) { checkpoint(); delete_range(ed_cursor, end); }
        return;
    }
    if (key == 'J') {
        if (end < ed_length) {
            checkpoint();
            delete_range(end, end + 1);
            while (end < ed_length && ed_buffer[end] == ' ') delete_range(end, end + 1);
            insert_at(end, ' ');
            if (ed_cursor) ed_cursor--;
        }
        return;
    }
    if (key == 'w') { move_word_forward(); return; }
    if (key == 'b') { move_word_backward(); return; }
    if (key == 'u') {
        if (ed_undo_valid) {
            for (usize i = 0; i <= ed_length; i++) ed_redo[i] = ed_buffer[i];
            ed_redo_length = ed_length; ed_redo_cursor = ed_cursor; ed_redo_valid = 1;
            for (usize i = 0; i <= ed_undo_length; i++) ed_buffer[i] = ed_undo[i];
            ed_length = ed_undo_length; ed_cursor = ed_undo_cursor; ed_undo_valid = 0;
            ed_dirty = 1; set_message("Undo");
        } else set_message("Nothing to undo");
        return;
    }
    if (key == 18) {
        if (ed_redo_valid) {
            checkpoint();
            for (usize i = 0; i <= ed_redo_length; i++) ed_buffer[i] = ed_redo[i];
            ed_length = ed_redo_length; ed_cursor = ed_redo_cursor; ed_redo_valid = 0;
            ed_dirty = 1; set_message("Redo");
        }
        return;
    }
    if (key == 'd' || key == 'y') { *pending = key; return; }
    if (key == 'p' || key == 'P') {
        usize n = str_len(ed_clipboard);
        if (n) {
            checkpoint();
            usize pos;
            if (key == 'P') {
                pos = line_start(ed_cursor);
            } else {
                pos = line_end(ed_cursor);
                if (pos < ed_length && ed_buffer[pos] == '\n') pos++;
                else if (pos == ed_length && ed_length && ed_buffer[pos - 1] != '\n') {
                    insert_at(pos, '\n');
                    pos++;
                }
            }
            for (usize i = 0; i < n && pos + i < ED_CAP - 1; i++)
                insert_at(pos + i, ed_clipboard[i]);
            ed_cursor = pos;
            if (ed_cursor < ed_length && ed_buffer[ed_cursor] == '\n' && ed_cursor + 1 < ed_length) ed_cursor++;
        }
        return;
    }
    if (key == 'r') {
        set_message("Replace: press a character");
        render();
        int replacement = serial_getkey();
        if (replacement >= 32 && replacement < 127 && ed_cursor < end) {
            checkpoint(); ed_buffer[ed_cursor] = (char)replacement; ed_dirty = 1;
        }
        return;
    }
    if (key == 'g') { *pending = 'g'; return; }
    if (key == 'G') {
        usize last_line = total_lines() - 1;
        ed_cursor = line_start_number(last_line);
        usize last_end = line_end(ed_cursor);
        if (last_end > ed_cursor) ed_cursor = last_end - 1;
        return;
    }
    if (key == 'n') { do_search(ed_search_reverse); return; }
    if (key == 'N') { do_search(!ed_search_reverse); return; }
    if (key == ':') { ed_mode = MODE_COMMAND; ed_command[0] = 0; return; }
    if (key == '/') { ed_mode = MODE_COMMAND; ed_command[0] = '/'; ed_command[1] = 0; return; }
    if (key == '?') { ed_mode = MODE_COMMAND; ed_command[0] = '?'; ed_command[1] = 0; return; }
    if (key == 22) { /* Ctrl-V: simple visual character selection is not implemented. */
        set_message("Visual mode is not implemented yet"); return;
    }
}

static void pending_command(int key, int *pending) {
    int command = *pending;
    *pending = 0;
    if (command == 'g') {
        if (key == 'g') ed_cursor = 0;
        else set_message("Use gg to go to the first line");
        return;
    }
    if (command == 'd' || command == 'y') {
        if (key != command) { set_message("Use dd or yy"); return; }
        usize start = line_start(ed_cursor);
        usize end = line_end(ed_cursor);
        if (end < ed_length) end++;
        usize n = end - start;
        if (command == 'y') {
            usize copy_n = n < sizeof(ed_clipboard) - 1 ? n : sizeof(ed_clipboard) - 1;
            for (usize i = 0; i < copy_n; i++) ed_clipboard[i] = ed_buffer[start + i];
            ed_clipboard[copy_n] = 0; set_message("1 line yanked");
        } else {
            checkpoint(); delete_range(start, end); set_message("1 line deleted");
        }
    }
}

void editor_open(const char *name) {
    const char *old;
    usize i = 0;
    int pending = 0;
    str_copy(ed_filename, name, sizeof(ed_filename));
    old = fs_read(name);
    if (!old) {
        ed_buffer[0] = 0;
        ed_length = 0;
        fs_touch(name);
    } else {
        while (old[i] && i + 1 < ED_CAP) { ed_buffer[i] = old[i]; i++; }
        ed_buffer[i] = 0;
        ed_length = i;
    }
    ed_cursor = 0;
    ed_top_line = 0;
    ed_mode = MODE_NORMAL;
    ed_dirty = 0;
    ed_show_numbers = 1;
    ed_block_comment = 0;
    ed_undo_valid = 0;
    ed_redo_valid = 0;
    ed_insert_checkpoint = 0;
    ed_completion_enabled = 1;
    ed_completion_dismissed = 0;
    ed_completion_dismissed_prefix[0] = 0;
    completion_clear();
    ed_search[0] = 0;
    ed_message[0] = 0;
    ed_clipboard[0] = 0;

    /* Repainting all 80 columns is useful on VGA, but noisy on a serial log. */
    serial_set_mirror(0);
    serial_cursor_enable(1);
    render();
    for (;;) {
        int key = serial_getkey();
        if (ed_mode == MODE_INSERT) insert_mode_key(key);
        else if (ed_mode == MODE_COMMAND) command_key(key);
        else if (pending) pending_command(key, &pending);
        else normal_mode_key(key, &pending);
        if (ed_mode == -1) break;
        completion_refresh();
        render();
    }
    serial_write("\033[0m\033[2J\033[H");
    serial_cursor_enable(1);
    serial_set_mirror(1);
}
