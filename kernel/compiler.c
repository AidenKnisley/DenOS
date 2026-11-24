#include "cvm.h"
#include "types.h"

/*
 * Aiden C - the freestanding C subset compiler used by DenOS.
 *
 * This compiler deliberately uses fixed-size tables and emits AEX6 stack
 * bytecode. The VM also accepts AEX1-AEX5 files from earlier releases for compatibility.
 */
#define MAX_CODE       768
#define MAX_STRINGS    2048
#define MAX_SYMBOLS    160
#define MAX_NAME       31
#define TOK_TEXT       128
#define MAX_CONTROL    32
#define MAX_PATCHES    96
#define MAX_FUNCTIONS  32
#define MAX_PARAMS     16
#define MAX_CALL_PATCHES 128
#define ERROR_TEXT     160
#define MAX_GLOBALS    64
#define MAX_GLOBAL_CELLS 256
#define MAX_SOURCE_BYTES 8192
#define MAX_MACROS 48
#define MAX_MACRO_VALUE 128
#define MAX_PP_DEPTH 16
#define MAX_TYPEDEFS 32

#define BC_PUSHI       1
#define BC_PUSHS       2
#define BC_PUSHADDR    3
#define BC_LOAD        4
#define BC_STORE       5
#define BC_ALOAD       6
#define BC_ASTORE      7
#define BC_ADD         8
#define BC_SUB         9
#define BC_MUL         10
#define BC_DIV         11
#define BC_MOD         12
#define BC_EQ          13
#define BC_NE          14
#define BC_LT          15
#define BC_LE          16
#define BC_GT          17
#define BC_GE          18
#define BC_AND         19
#define BC_OR          20
#define BC_NOT         21
#define BC_NEG         22
#define BC_JMP         23
#define BC_JZ          24
#define BC_CALL        25
#define BC_POP         26
#define BC_RET         27
#define BC_BITAND      28
#define BC_BITOR       29
#define BC_BITXOR      30
#define BC_SHL         31
#define BC_SHR         32
#define BC_BITNOT      33
#define BC_DUP         34
#define BC_PUSHI64     35
#define BC_CALL_USER   36
#define BC_ENTER       37
#define BC_BOUNDS      38
#define BC_DEREF       39
#define BC_STOREPTR    40
#define BC_GLOAD       41
#define BC_GSTORE      42
#define BC_GADDR       43

enum { T_EOF = 0, T_ID = 256, T_NUM, T_STRING, T_CHAR };
enum { CTRL_LOOP = 1, CTRL_SWITCH = 2 };
#define MAX_SWITCH_CASES 64
#define MAX_ENUM_CONSTANTS 128

typedef struct {
    u8 op;
    i32 arg;
} Instruction;

typedef struct {
    int kind;
    char text[TOK_TEXT];
    int length;
    u64 value;
    int line;
    int column;
} Token;

typedef struct {
    char name[MAX_NAME + 1];
    int base;
    int count;
    int depth;
    int is_array;
    int pointer_depth;
    int is_global;
} Symbol;

typedef struct {
    i64 value;
    int target;
} SwitchCase;

typedef struct {
    int kind;
    int continue_target;
    int breaks[MAX_PATCHES];
    int break_count;
    int continues[MAX_PATCHES];
    int continue_count;
    int switch_slot;
    int default_target;
    int case_count;
    SwitchCase cases[MAX_SWITCH_CASES];
} Control;

typedef struct {
    char name[MAX_NAME + 1];
    i64 value;
} EnumConstant;

typedef struct {
    char name[MAX_NAME + 1];
    char value[MAX_MACRO_VALUE];
} Macro;

typedef struct {
    int parent_active;
    int condition;
    int saw_else;
} PreprocessorFrame;

typedef struct {
    char name[MAX_NAME + 1];
    int return_type;
    int parameter_count;
    int code_start;
    int local_count;
    int defined;
    int parameter_pointer_depth[MAX_PARAMS];
} Function;

typedef struct {
    char name[MAX_NAME + 1];
    int has_name;
    int pointer_depth;
} Parameter;

typedef struct {
    int instruction;
    int function_index;
    int argument_count;
} UserCallPatch;

static Instruction code[MAX_CODE];
static int code_count;
static char strings[MAX_STRINGS];
static int strings_count;
static Symbol symbols[MAX_SYMBOLS];
static int symbol_count;
static Symbol global_symbols[MAX_GLOBALS];
static i64 global_values[MAX_GLOBAL_CELLS];
static int global_symbol_count;
static int global_cell_count;
static Macro macros[MAX_MACROS];
static int macro_count;
static char typedef_names[MAX_TYPEDEFS][MAX_NAME + 1];
static int typedef_count;
static char preprocessed_source[MAX_SOURCE_BYTES];
static EnumConstant enum_constants[MAX_ENUM_CONSTANTS];
static int enum_constant_count;
static int memory_top;
static int scope_depth;
static Control controls[MAX_CONTROL];
static int control_count;
static int function_high_water;
static Function functions[MAX_FUNCTIONS];
static int function_count;
static UserCallPatch user_call_patches[MAX_CALL_PATCHES];
static int user_call_patch_count;
static int main_function_index;

static const char *source_ptr;
static Token tok;
static int source_line;
static int source_column;
static int previous_token_line;
static int previous_token_column;
static int failed;
static char error_text[ERROR_TEXT];

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void set_error_at(int line, int column, const char *message);

static int pp_identifier_start(char c) { return is_alpha(c); }
static int pp_identifier_char(char c) { return is_alpha(c) || is_digit(c); }

static int macro_find(const char *name) {
    for (int i = macro_count - 1; i >= 0; --i)
        if (streq(macros[i].name, name)) return i;
    return -1;
}

static int macro_set(const char *name, const char *value, int line) {
    int index = macro_find(name);
    if (index < 0) {
        if (macro_count >= MAX_MACROS) { set_error_at(line, 1, "macro table is full (maximum 48)"); return -1; }
        index = macro_count++;
    }
    int n = 0;
    while (name[n] && n < MAX_NAME) { macros[index].name[n] = name[n]; ++n; }
    if (name[n]) { set_error_at(line, 1, "macro name exceeds 31 characters"); return -1; }
    macros[index].name[n] = 0;
    n = 0;
    while (value[n] && n < MAX_MACRO_VALUE - 1) { macros[index].value[n] = value[n]; ++n; }
    if (value[n]) { set_error_at(line, 1, "macro replacement exceeds 127 characters"); return -1; }
    macros[index].value[n] = 0;
    return index;
}

static int pp_append(char *out, int *used, const char *text, int length, int line) {
    if (length < 0 || *used + length >= MAX_SOURCE_BYTES) {
        set_error_at(line, 1, "preprocessed source exceeds 8191 bytes");
        return 0;
    }
    for (int i = 0; i < length; ++i) out[(*used)++] = text[i];
    out[*used] = 0;
    return 1;
}

static int pp_expand_text(const char *input, char *out, int *used, int line,
                          int *in_block_comment, int depth) {
    if (depth > 8) { set_error_at(line, 1, "recursive macro expansion detected"); return 0; }
    int i = 0;
    while (input[i] && !failed) {
        if (*in_block_comment) {
            if (input[i] == '*' && input[i + 1] == '/') {
                if (!pp_append(out, used, input + i, 2, line)) return 0;
                i += 2; *in_block_comment = 0;
            } else {
                if (!pp_append(out, used, input + i, 1, line)) return 0;
                ++i;
            }
            continue;
        }
        if (input[i] == '/' && input[i + 1] == '*') {
            if (!pp_append(out, used, input + i, 2, line)) return 0;
            i += 2; *in_block_comment = 1;
            continue;
        }
        if (input[i] == '/' && input[i + 1] == '/') {
            int rest = 0;
            while (input[i + rest]) ++rest;
            return pp_append(out, used, input + i, rest, line);
        }
        if (input[i] == '"' || input[i] == '\'') {
            char quote = input[i];
            if (!pp_append(out, used, input + i, 1, line)) return 0;
            ++i;
            while (input[i]) {
                char c = input[i];
                if (!pp_append(out, used, input + i, 1, line)) return 0;
                ++i;
                if (c == '\\' && input[i]) {
                    if (!pp_append(out, used, input + i, 1, line)) return 0;
                    ++i;
                } else if (c == quote) break;
            }
            continue;
        }
        if (pp_identifier_start(input[i])) {
            int start = i++;
            while (pp_identifier_char(input[i])) ++i;
            char name[MAX_NAME + 1];
            int length = i - start;
            if (length > MAX_NAME) {
                if (!pp_append(out, used, input + start, length, line)) return 0;
                continue;
            }
            for (int n = 0; n < length; ++n) name[n] = input[start + n];
            name[length] = 0;
            int m = macro_find(name);
            if (m < 0) {
                if (!pp_append(out, used, input + start, length, line)) return 0;
            } else if (!pp_expand_text(macros[m].value, out, used, line, in_block_comment, depth + 1)) {
                return 0;
            }
            continue;
        }
        if (!pp_append(out, used, input + i, 1, line)) return 0;
        ++i;
    }
    return !failed;
}

static int pp_word(const char **cursor, char *word, int capacity) {
    const char *p = *cursor;
    while (*p == ' ' || *p == '\t') ++p;
    int n = 0;
    while (pp_identifier_char(*p)) {
        if (n + 1 < capacity) word[n++] = *p;
        ++p;
    }
    word[n] = 0;
    *cursor = p;
    return n;
}

static void preprocess_source(const char *src) {
    PreprocessorFrame stack[MAX_PP_DEPTH];
    int depth = 0;
    int active = 1;
    int in_block_comment = 0;
    int used = 0;
    int line_number = 1;
    const char *cursor = src;
    macro_count = 0;
    preprocessed_source[0] = 0;

    while (*cursor && !failed) {
        const char *line = cursor;
        while (*cursor && *cursor != '\n') ++cursor;
        int length = (int)(cursor - line);
        int had_newline = *cursor == '\n';
        if (had_newline) ++cursor;
        char raw_line[MAX_SOURCE_BYTES];
        for (int i = 0; i < length; ++i) raw_line[i] = line[i];
        raw_line[length] = 0;
        int first = 0;
        while (first < length && (raw_line[first] == ' ' || raw_line[first] == '\t' || raw_line[first] == '\r')) ++first;

        if (first < length && raw_line[first] == '#') {
            const char *directive = raw_line + first + 1;
            char command[16], name[MAX_NAME + 1];
            pp_word(&directive, command, sizeof(command));
            while (*directive == ' ' || *directive == '\t') ++directive;
            if (streq(command, "if") || streq(command, "ifdef") || streq(command, "ifndef")) {
                if (depth >= MAX_PP_DEPTH) { set_error_at(line_number, first + 1, "conditional nesting exceeds 16 levels"); break; }
                int condition = 0;
                if (streq(command, "if")) {
                    if (*directive == '0' && directive[1] == 0) condition = 0;
                    else if (*directive == '1' && directive[1] == 0) condition = 1;
                    else { set_error_at(line_number, first + 1, "#if currently accepts only literal 0 or 1; use #ifdef for macros"); break; }
                } else {
                    pp_word(&directive, name, sizeof(name));
                    if (!name[0]) { set_error_at(line_number, first + 1, "conditional directive needs a macro name"); break; }
                    condition = macro_find(name) >= 0;
                    if (streq(command, "ifndef")) condition = !condition;
                }
                stack[depth].parent_active = active;
                stack[depth].condition = condition;
                stack[depth].saw_else = 0;
                ++depth;
                active = active && condition;
            } else if (streq(command, "else")) {
                if (!depth || stack[depth - 1].saw_else) { set_error_at(line_number, first + 1, "unmatched or duplicate #else"); break; }
                stack[depth - 1].saw_else = 1;
                active = stack[depth - 1].parent_active && !stack[depth - 1].condition;
            } else if (streq(command, "endif")) {
                if (!depth) { set_error_at(line_number, first + 1, "unmatched #endif"); break; }
                --depth;
                active = stack[depth].parent_active;
            } else if (!active) {
                /* Directives in a disabled conditional group are not evaluated. */
            } else if (streq(command, "define")) {
                int name_length = pp_word(&directive, name, sizeof(name));
                if (!name_length) { set_error_at(line_number, first + 1, "#define needs a macro name"); break; }
                if (*directive == '(') { set_error_at(line_number, first + 1, "function-like macros are not supported yet"); break; }
                while (*directive == ' ' || *directive == '\t') ++directive;
                char value[MAX_MACRO_VALUE];
                int n = 0;
                while (*directive && *directive != '\r' && n < MAX_MACRO_VALUE - 1) value[n++] = *directive++;
                while (n > 0 && (value[n - 1] == ' ' || value[n - 1] == '\t')) --n;
                value[n] = 0;
                if (*directive && *directive != '\r') { set_error_at(line_number, first + 1, "macro replacement is too long"); break; }
                if (macro_set(name, value, line_number) < 0) break;
            } else if (streq(command, "undef")) {
                pp_word(&directive, name, sizeof(name));
                int m = macro_find(name);
                if (m >= 0) {
                    for (int i = m; i + 1 < macro_count; ++i) macros[i] = macros[i + 1];
                    --macro_count;
                }
            } else if (streq(command, "include") || streq(command, "pragma")) {
                /* Headers are currently API hints only; no hosted libc is linked. */
            } else if (streq(command, "error")) {
                set_error_at(line_number, first + 1, "encountered #error directive");
                break;
            } else {
                set_error_at(line_number, first + 1, "unsupported preprocessor directive");
                break;
            }
        } else if (active) {
            char line_buffer[MAX_SOURCE_BYTES];
            int line_used = 0;
            line_buffer[0] = 0;
            if (!pp_expand_text(raw_line, line_buffer, &line_used, line_number, &in_block_comment, 0)) break;
            if (!pp_append(preprocessed_source, &used, line_buffer, line_used, line_number)) break;
        }
        if (had_newline && !pp_append(preprocessed_source, &used, "\n", 1, line_number)) break;
        ++line_number;
    }
    if (!failed && depth) set_error_at(line_number, 1, "unterminated conditional preprocessor block");
    if (!failed && in_block_comment) set_error_at(line_number, 1, "unterminated block comment");
}


static void append_char(char *out, int *pos, int cap, char c) {
    if (*pos + 1 < cap) out[(*pos)++] = c;
}

static void append_text(char *out, int *pos, int cap, const char *s) {
    while (*s) append_char(out, pos, cap, *s++);
}

static void append_uint(char *out, int *pos, int cap, unsigned value) {
    char digits[12];
    int n = 0;
    do {
        digits[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && n < (int)sizeof(digits));
    while (n) append_char(out, pos, cap, digits[--n]);
}

static void set_error_at(int line, int column, const char *message) {
    if (failed) return;
    failed = 1;
    int p = 0;
    append_text(error_text, &p, ERROR_TEXT, "line ");
    append_uint(error_text, &p, ERROR_TEXT, (unsigned)(line > 0 ? line : 1));
    append_text(error_text, &p, ERROR_TEXT, ", column ");
    append_uint(error_text, &p, ERROR_TEXT, (unsigned)(column > 0 ? column : 1));
    append_text(error_text, &p, ERROR_TEXT, ": ");
    append_text(error_text, &p, ERROR_TEXT, message);
    error_text[p] = 0;
}

static void set_error(const char *message) {
    set_error_at(tok.line ? tok.line : source_line,
                 tok.column ? tok.column : source_column, message);
}

static void set_expected(const char *what) {
    char message[96];
    int p = 0;
    append_text(message, &p, (int)sizeof(message), "expected '");
    append_text(message, &p, (int)sizeof(message), what);
    append_text(message, &p, (int)sizeof(message), "'");
    message[p] = 0;
    set_error(message);
}

static char take_char(void) {
    char c = *source_ptr;
    if (c) source_ptr++;
    if (c == '\n') {
        source_line++;
        source_column = 1;
    } else if (c) {
        source_column++;
    }
    return c;
}

static void skip_space(void) {
    for (;;) {
        while (is_space(*source_ptr)) take_char();

        /* Preprocessor directives are intentionally ignored by this subset. */
        if (*source_ptr == '#') {
            while (*source_ptr && take_char() != '\n') { }
            continue;
        }
        if (source_ptr[0] == '/' && source_ptr[1] == '/') {
            while (*source_ptr && take_char() != '\n') { }
            continue;
        }
        if (source_ptr[0] == '/' && source_ptr[1] == '*') {
            int start_line = source_line;
            int start_column = source_column;
            take_char();
            take_char();
            while (*source_ptr && !(source_ptr[0] == '*' && source_ptr[1] == '/')) take_char();
            if (!*source_ptr) {
                set_error_at(start_line, start_column, "unterminated block comment");
                return;
            }
            take_char();
            take_char();
            continue;
        }
        break;
    }
}

static int read_escape(void) {
    char c = take_char();
    if (!c) {
        set_error("incomplete escape sequence");
        return 0;
    }
    switch (c) {
        case 'a': return 7;
        case 'b': return 8;
        case 'f': return 12;
        case 'n': return 10;
        case 'r': return 13;
        case 't': return 9;
        case 'v': return 11;
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case '?': return '?';
        case 'e': return 27; /* Common terminal escape extension. */
        case 'x': {
            int value = 0;
            int digits = 0;
            while (digit_value(*source_ptr) >= 0 && digit_value(*source_ptr) < 16) {
                int d = digit_value(take_char());
                if (value > (255 - d) / 16) {
                    set_error("hex escape does not fit in one byte");
                    return 0;
                }
                value = value * 16 + d;
                digits++;
            }
            if (!digits) set_error("hex escape needs at least one digit");
            return value;
        }
        default:
            if (c >= '0' && c <= '7') {
                int value = c - '0';
                int count = 1;
                while (count < 3 && *source_ptr >= '0' && *source_ptr <= '7') {
                    value = value * 8 + (take_char() - '0');
                    count++;
                }
                if (value > 255) set_error("octal escape does not fit in one byte");
                return value & 255;
            }
            set_error("unknown escape sequence");
            return (u8)c;
    }
}

static int is_suffix(char c) {
    return c == 'u' || c == 'U' || c == 'l' || c == 'L';
}

static void next_token(void) {
    previous_token_line = tok.line;
    previous_token_column = tok.column;
    skip_space();
    tok.text[0] = 0;
    tok.length = 0;
    tok.value = 0;
    tok.line = source_line;
    tok.column = source_column;

    char c = *source_ptr;
    if (!c) {
        tok.kind = T_EOF;
        return;
    }

    if (is_alpha(c)) {
        int n = 0;
        while (is_alpha(*source_ptr) || is_digit(*source_ptr)) {
            if (n < TOK_TEXT - 1) tok.text[n++] = take_char();
            else {
                take_char();
                set_error("identifier is too long");
            }
        }
        tok.text[n] = 0;
        tok.length = n;
        if (n > MAX_NAME) set_error("identifier is too long (maximum 31 characters)");
        tok.kind = T_ID;
        return;
    }

    if (is_digit(c)) {
        unsigned base = 10;
        int had_prefix = 0;
        if (source_ptr[0] == '0' && (source_ptr[1] == 'x' || source_ptr[1] == 'X')) {
            base = 16;
            take_char(); take_char();
            had_prefix = 1;
        } else if (source_ptr[0] == '0' && (source_ptr[1] == 'b' || source_ptr[1] == 'B')) {
            base = 2;
            take_char(); take_char();
            had_prefix = 1;
        } else if (source_ptr[0] == '0' && is_digit(source_ptr[1])) {
            base = 8;
        }

        u64 value = 0;
        int digits = 0;
        for (;;) {
            int d = digit_value(*source_ptr);
            if (d < 0 || (unsigned)d >= base) break;
            take_char();
            if (value > (0x7FFFFFFFFFFFFFFFULL - (u64)d) / base) {
                set_error("integer literal exceeds the signed 64-bit range");
                while (is_alpha(*source_ptr) || is_digit(*source_ptr)) take_char();
                tok.kind = T_NUM;
                tok.value = 0;
                return;
            }
            value = value * base + (u64)d;
            digits++;
        }
        if (!digits) {
            set_error(had_prefix ? "numeric base prefix needs digits" : "invalid integer literal");
        }
        while (is_suffix(*source_ptr)) take_char();
        if (is_alpha(*source_ptr) || is_digit(*source_ptr)) {
            set_error("invalid character in integer literal");
            while (is_alpha(*source_ptr) || is_digit(*source_ptr)) take_char();
        }
        tok.kind = T_NUM;
        tok.value = value;
        return;
    }

    if (c == '"') {
        take_char();
        int n = 0;
        while (*source_ptr && *source_ptr != '"') {
            int value;
            if (*source_ptr == '\\') {
                take_char();
                value = read_escape();
            } else {
                if (*source_ptr == '\n' || *source_ptr == '\r') {
                    set_error("newline inside string literal");
                    break;
                }
                value = (u8)take_char();
            }
            if (n < TOK_TEXT - 1) tok.text[n++] = (char)value;
            else set_error("string literal is too long (maximum 127 bytes)");
        }
        if (*source_ptr == '"') take_char();
        else set_error("unterminated string literal");
        tok.text[n] = 0;
        tok.length = n;
        tok.kind = T_STRING;
        return;
    }

    if (c == '\'') {
        take_char();
        int value;
        if (!*source_ptr || *source_ptr == '\n' || *source_ptr == '\r') {
            set_error("empty or unterminated character literal");
            value = 0;
        } else if (*source_ptr == '\\') {
            take_char();
            value = read_escape();
        } else {
            value = (u8)take_char();
        }
        if (*source_ptr == '\'') take_char();
        else {
            set_error("character literal must contain exactly one byte");
            while (*source_ptr && *source_ptr != '\'' && *source_ptr != '\n') take_char();
            if (*source_ptr == '\'') take_char();
        }
        tok.kind = T_CHAR;
        tok.value = (u8)value;
        return;
    }

    char first = take_char();
    tok.kind = (u8)first;
    tok.text[0] = first;
    tok.text[1] = 0;
    tok.length = 1;

    /* Longest-match operator recognition. */
    if ((first == '<' && *source_ptr == '<') || (first == '>' && *source_ptr == '>')) {
        char second = take_char();
        tok.text[1] = second;
        tok.text[2] = 0;
        tok.length = 2;
        if (*source_ptr == '=') {
            tok.text[2] = take_char();
            tok.text[3] = 0;
            tok.length = 3;
        }
        return;
    }

    if ((first == '+' && (*source_ptr == '+' || *source_ptr == '=')) ||
        (first == '-' && (*source_ptr == '-' || *source_ptr == '=')) ||
        ((first == '*' || first == '/' || first == '%' || first == '&' ||
          first == '|' || first == '^') && *source_ptr == '=') ||
        ((first == '=' || first == '!' || first == '<' || first == '>') && *source_ptr == '=') ||
        (first == '&' && *source_ptr == '&') ||
        (first == '|' && *source_ptr == '|')) {
        tok.text[1] = take_char();
        tok.text[2] = 0;
        tok.length = 2;
    }
}

static int accept(const char *text) {
    if (streq(tok.text, text)) {
        next_token();
        return 1;
    }
    return 0;
}

static void expect(const char *text) {
    if (!accept(text)) set_expected(text);
}

static int emit(int op, i32 arg) {
    if (failed) return -1;
    if (code_count >= MAX_CODE) {
        set_error("program is too large (AEX limit is 768 instructions)");
        return -1;
    }
    code[code_count].op = (u8)op;
    code[code_count].arg = arg;
    return code_count++;
}

static void patch(int at, int target) {
    if (at >= 0 && at < code_count) code[at].arg = target;
}

static int add_string(const char *text, int length) {
    if (length < 0 || strings_count + length + 1 > MAX_STRINGS) {
        set_error("string/data table is full");
        return -1;
    }
    int start = strings_count;
    for (int i = 0; i < length; i++) strings[strings_count++] = text[i];
    strings[strings_count++] = 0;
    return start;
}

static int add_i64_constant(u64 value) {
    if (strings_count + 8 > MAX_STRINGS) {
        set_error("constant table is full");
        return -1;
    }
    int start = strings_count;
    for (int i = 0; i < 8; i++) strings[strings_count++] = (char)(value >> (i * 8));
    return start;
}

static int emit_integer(u64 value) {
    if (value <= 0x7FFFFFFFULL) return emit(BC_PUSHI, (i32)value);
    int offset = add_i64_constant(value);
    if (offset < 0) return -1;
    return emit(BC_PUSHI64, offset);
}

static void enter_scope(void) { scope_depth++; }

static void leave_scope(int saved_symbols, int saved_memory) {
    symbol_count = saved_symbols;
    memory_top = saved_memory;
    if (scope_depth > 0) scope_depth--;
}

static int find_symbol(const char *name) {
    for (int i = symbol_count - 1; i >= 0; i--) {
        if (streq(symbols[i].name, name)) return i;
    }
    return -1;
}

static int add_symbol(const char *name, int count, int is_array) {
    if (symbol_count >= MAX_SYMBOLS) {
        set_error("too many variables (maximum 160 active declarations)");
        return -1;
    }
    for (int i = symbol_count - 1; i >= 0 && symbols[i].depth == scope_depth; i--) {
        if (streq(symbols[i].name, name)) {
            set_error("duplicate variable in the same block");
            return -1;
        }
    }
    if (count < 1 || count > 4096 - memory_top) {
        set_error("too much variable storage (maximum 4096 cells)");
        return -1;
    }
    int n = 0;
    while (name[n] && n < MAX_NAME) {
        symbols[symbol_count].name[n] = name[n];
        n++;
    }
    if (name[n]) {
        set_error("identifier is too long (maximum 31 characters)");
        return -1;
    }
    symbols[symbol_count].name[n] = 0;
    symbols[symbol_count].base = memory_top;
    symbols[symbol_count].count = count;
    symbols[symbol_count].depth = scope_depth;
    symbols[symbol_count].is_array = is_array;
    memory_top += count;
    if (memory_top > function_high_water) function_high_water = memory_top;
    symbols[symbol_count].pointer_depth = 0;
    symbols[symbol_count].is_global = 0;
    return symbol_count++;
}

static int add_global_symbol(const char *name, int count, int is_array, int pointer_depth) {
    if (global_symbol_count >= MAX_GLOBALS) {
        set_error("too many global declarations (maximum 64)");
        return -1;
    }
    for (int i = 0; i < global_symbol_count; ++i) {
        if (streq(global_symbols[i].name, name)) {
            set_error("duplicate global variable declaration");
            return -1;
        }
    }
    if (count < 1 || count > MAX_GLOBAL_CELLS - global_cell_count) {
        set_error("global storage limit exceeded (maximum 256 cells)");
        return -1;
    }
    int n = 0;
    while (name[n] && n < MAX_NAME) {
        global_symbols[global_symbol_count].name[n] = name[n];
        ++n;
    }
    if (name[n]) { set_error("identifier is too long (maximum 31 characters)"); return -1; }
    global_symbols[global_symbol_count].name[n] = 0;
    global_symbols[global_symbol_count].base = global_cell_count;
    global_symbols[global_symbol_count].count = count;
    global_symbols[global_symbol_count].depth = -1;
    global_symbols[global_symbol_count].is_array = is_array;
    global_symbols[global_symbol_count].pointer_depth = pointer_depth;
    global_symbols[global_symbol_count].is_global = 1;
    for (int i = 0; i < count; ++i) global_values[global_cell_count + i] = 0;
    global_cell_count += count;
    return global_symbol_count++;
}

static int require_symbol(const char *name) {
    int index = find_symbol(name);
    if (index < 0) {
        char message[80];
        int p = 0;
        append_text(message, &p, (int)sizeof(message), "unknown variable '");
        append_text(message, &p, (int)sizeof(message), name);
        append_text(message, &p, (int)sizeof(message), "'");
        message[p] = 0;
        set_error_at(previous_token_line, previous_token_column, message);
    }
    return index;
}

static int is_type_start(void);
static int parse_type(int allow_void);

static int find_typedef(const char *name) {
    for (int i = typedef_count - 1; i >= 0; --i)
        if (streq(typedef_names[i], name)) return i;
    return -1;
}

static void add_typedef_name(const char *name) {
    if (find_typedef(name) >= 0) { set_error("duplicate typedef name"); return; }
    if (typedef_count >= MAX_TYPEDEFS) { set_error("typedef table is full (maximum 32)"); return; }
    int n = 0;
    while (name[n] && n < MAX_NAME) { typedef_names[typedef_count][n] = name[n]; ++n; }
    if (name[n]) { set_error("typedef name exceeds 31 characters"); return; }
    typedef_names[typedef_count][n] = 0;
    ++typedef_count;
}

static void parse_typedef_declaration(void) {
    if (!is_type_start()) { set_error("typedef needs an existing scalar type"); return; }
    if (parse_type(0) < 0) return;
    if (accept("*")) { set_error("pointer typedefs are not supported yet"); return; }
    if (tok.kind != T_ID) { set_error("typedef needs a name"); return; }
    char name[MAX_NAME + 1];
    for (int i = 0; i <= tok.length; ++i) name[i] = tok.text[i];
    next_token();
    if (streq(tok.text, "[")) { set_error("array typedefs are not supported yet"); return; }
    add_typedef_name(name);
    expect(";");
}

static int is_type_start(void) {
    return streq(tok.text, "int") || streq(tok.text, "char") ||
           streq(tok.text, "long") || streq(tok.text, "short") ||
           streq(tok.text, "signed") || streq(tok.text, "unsigned") ||
           streq(tok.text, "void") || streq(tok.text, "const") ||
           streq(tok.text, "enum") || find_typedef(tok.text) >= 0;
}

static int find_enum_constant(const char *name) {
    for (int i = enum_constant_count - 1; i >= 0; --i)
        if (streq(enum_constants[i].name, name)) return i;
    return -1;
}

static int add_enum_constant(const char *name, i64 value) {
    if (find_enum_constant(name) >= 0) {
        set_error("duplicate enumerator name");
        return -1;
    }
    if (enum_constant_count >= MAX_ENUM_CONSTANTS) {
        set_error("too many enum constants (maximum 128)");
        return -1;
    }
    EnumConstant *constant = &enum_constants[enum_constant_count++];
    int i = 0;
    while (name[i] && i < MAX_NAME) { constant->name[i] = name[i]; i++; }
    constant->name[i] = 0;
    constant->value = value;
    return enum_constant_count - 1;
}

static int parse_enum_constant_value(i64 *out) {
    int negative = accept("-");
    if (tok.kind == T_NUM || tok.kind == T_CHAR) {
        u64 value = tok.value;
        next_token();
        if (value > 0x7FFFFFFFFFFFFFFFULL && !(negative && value == 0x8000000000000000ULL)) {
            set_error("enum value is outside the signed 64-bit range");
            return 0;
        }
        *out = negative ? (value == 0x8000000000000000ULL
                            ? (-9223372036854775807LL - 1LL) : -(i64)value)
                        : (i64)value;
        return 1;
    }
    if (!negative && tok.kind == T_ID) {
        int index = find_enum_constant(tok.text);
        if (index >= 0) { *out = enum_constants[index].value; next_token(); return 1; }
    }
    set_error("expected an integer constant in enum declaration");
    return 0;
}

/* Aiden C uses one signed 64-bit VM cell for every scalar integer type. */
static int parse_type(int allow_void) {
    if (find_typedef(tok.text) >= 0) { next_token(); return 1; }
    if (accept("enum")) {
        if (tok.kind == T_ID) next_token(); /* Optional tag name. */
        if (accept("{")) {
            i64 next_value = 0;
            if (!accept("}")) {
                do {
                    if (tok.kind != T_ID) { set_error("expected enum constant name"); return -1; }
                    char name[MAX_NAME + 1];
                    int n = tok.length;
                    for (int i = 0; i <= n; i++) name[i] = tok.text[i];
                    next_token();
                    i64 value = next_value;
                    if (accept("=") && !parse_enum_constant_value(&value)) return -1;
                    if (add_enum_constant(name, value) < 0) return -1;
                    if (value == 0x7FFFFFFFFFFFFFFFLL && !streq(tok.text, "}")) {
                        set_error("next implicit enum value would overflow"); return -1;
                    }
                    next_value = value == 0x7FFFFFFFFFFFFFFFLL ? value : value + 1;
                } while (accept(",") && !streq(tok.text, "}") && !failed);
                expect("}");
            }
        }
        return failed ? -1 : 1;
    }
    int saw_signed = 0, saw_unsigned = 0, saw_char = 0, saw_short = 0;
    int long_count = 0, saw_int = 0, saw_void = 0, saw_const = 0;
    int consumed = 0;
    while (is_type_start()) {
        if (accept("const")) { if (saw_const) set_error("duplicate const qualifier"); saw_const = 1; }
        else if (accept("signed")) { if (saw_signed || saw_unsigned) set_error("conflicting signedness modifiers"); saw_signed = 1; }
        else if (accept("unsigned")) { if (saw_unsigned || saw_signed) set_error("conflicting signedness modifiers"); saw_unsigned = 1; }
        else if (accept("char")) { if (saw_char || saw_int || saw_short || long_count || saw_void) set_error("invalid integer type combination"); saw_char = 1; }
        else if (accept("short")) { if (saw_short || saw_char || long_count || saw_void) set_error("invalid integer type combination"); saw_short = 1; }
        else if (accept("long")) { if (saw_char || saw_short || saw_void || long_count >= 2) set_error("invalid integer type combination"); long_count++; }
        else if (accept("int")) { if (saw_int || saw_char || saw_void) set_error("invalid integer type combination"); saw_int = 1; }
        else if (accept("void")) { if (saw_void || saw_char || saw_short || long_count || saw_int || saw_signed || saw_unsigned) set_error("invalid void type combination"); saw_void = 1; }
        else break;
        consumed++;
        if (failed) return -1;
    }
    if (!consumed || (saw_const && !saw_signed && !saw_unsigned && !saw_char &&
                      !saw_short && !long_count && !saw_int && !saw_void)) {
        set_error("expected a type name");
        return -1;
    }
    if (saw_void && !allow_void) {
        set_error("void variables are not supported");
        return -1;
    }
    return saw_void ? 0 : 1;
}

static int parse_expression(void);
static int parse_conditional(void);
static void parse_statement(void);

static int builtin_id(const char *name) {
    static const char *const names[] = {
        "", "printf", "puts", "putchar", "scanf", "getchar",
        "strlen", "strcmp", "strncmp", "strcpy", "strncpy", "strcat",
        "strchr", "strrchr", "strstr", "abs", "labs", "atoi",
        "isdigit", "isalpha", "isspace", "toupper", "tolower",
        "isalnum", "isupper", "islower", "isxdigit", "iscntrl",
        "isprint", "isgraph", "ispunct"
    };
    for (int i = 1; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
        if (streq(name, names[i])) return i;
    return -1;
}

static void validate_call(int id, int argc) {
    int valid = 0;
    if (id == 1) valid = argc >= 1;
    else if (id == 2 || id == 3 || id == 6 || id == 15 || id == 16 ||
             id == 17 || (id >= 18 && id <= 30)) valid = argc == 1;
    else if (id == 4) valid = argc >= 2;
    else if (id == 5) valid = argc == 0;
    else if (id == 7 || id == 9 || id == 11 || id == 12 || id == 13 || id == 14)
        valid = argc == 2;
    else if (id == 8 || id == 10) valid = argc == 3;
    if (argc > 255) set_error("a function call may have at most 255 arguments");
    else if (id < 0 || !valid) set_error("unknown function or incorrect number of arguments");
}

static int find_function(const char *name) {
    for (int i = 0; i < function_count; i++) {
        if (streq(functions[i].name, name)) return i;
    }
    return -1;
}

static int declare_function(const char *name, int return_type,
                            Parameter params[MAX_PARAMS], int argc) {
    int index = find_function(name);
    if (builtin_id(name) >= 0) {
        set_error("a user function cannot reuse a built-in function name");
        return -1;
    }
    if (index >= 0) {
        if (functions[index].return_type != return_type ||
            functions[index].parameter_count != argc) {
            set_error("function declaration does not match its earlier prototype");
            return -1;
        }
        for (int i = 0; i < argc; i++) {
            if (functions[index].parameter_pointer_depth[i] != params[i].pointer_depth) {
                set_error("function pointer parameter does not match its earlier prototype");
                return -1;
            }
        }
        return index;
    }
    if (function_count >= MAX_FUNCTIONS) {
        set_error("too many functions (maximum 32)");
        return -1;
    }
    index = function_count++;
    int n = 0;
    while (name[n] && n < MAX_NAME) {
        functions[index].name[n] = name[n];
        n++;
    }
    functions[index].name[n] = 0;
    functions[index].return_type = return_type;
    functions[index].parameter_count = argc;
    functions[index].code_start = -1;
    functions[index].local_count = 0;
    functions[index].defined = 0;
    for (int i = 0; i < argc; i++)
        functions[index].parameter_pointer_depth[i] = params[i].pointer_depth;
    return index;
}

static void patch_function_calls(int function_index) {
    if (function_index < 0 || function_index >= function_count) return;
    for (int i = 0; i < user_call_patch_count; i++) {
        UserCallPatch *p = &user_call_patches[i];
        if (p->function_index == function_index) {
            int encoded = ((p->argument_count & 0xff) << 16) |
                          (functions[function_index].code_start & 0xffff);
            patch(p->instruction, encoded);
            p->function_index = -1;
        }
    }
}

static int parse_call(const char *name) {
    int id = builtin_id(name);
    int function_index = id < 0 ? find_function(name) : -1;
    int argc = 0;
    expect("(");
    if (!accept(")")) {
        do {
            parse_expression();
            argc++;
            if (argc > MAX_PARAMS) set_error("a function call may have at most 16 arguments");
        } while (accept(",") && !failed);
        expect(")");
    }
    if (id >= 0) {
        validate_call(id, argc);
        if (!failed) emit(BC_CALL, (id << 16) | (argc & 0xffff));
    } else if (function_index < 0) {
        set_error("unknown function (add a prototype before calling it)");
    } else if (argc != functions[function_index].parameter_count) {
        set_error("incorrect number of arguments for user-defined function");
    } else if (!failed) {
        int target = functions[function_index].code_start;
        int encoded = ((argc & 0xff) << 16) | ((target < 0 ? 0 : target) & 0xffff);
        int instruction = emit(BC_CALL_USER, encoded);
        if (target < 0 && instruction >= 0) {
            if (user_call_patch_count >= MAX_CALL_PATCHES) {
                set_error("too many calls to functions declared later");
            } else {
                UserCallPatch *p = &user_call_patches[user_call_patch_count++];
                p->instruction = instruction;
                p->function_index = function_index;
                p->argument_count = argc;
            }
        }
    }
    return 1;
}

static int parse_lvalue_after_name(int symbol) {
    if (symbol < 0) return 0;
    if (accept("[")) {
        if (symbols[symbol].is_array) {
            emit(symbols[symbol].is_global ? BC_GADDR : BC_PUSHADDR, symbols[symbol].base);
            parse_expression();
            expect("]");
            emit(BC_BOUNDS, symbols[symbol].count);
            emit(BC_ADD, 0);
            return 3; /* A checked pointer to an array cell. */
        }
        if (symbols[symbol].pointer_depth > 0) {
            emit(symbols[symbol].is_global ? BC_GLOAD : BC_LOAD, symbols[symbol].base);
            parse_expression();
            expect("]");
            emit(BC_ADD, 0);
            return 3; /* Pointer-derived address. */
        }
        set_error("subscripted value is not an array or pointer");
        return 0;
    }
    if (symbols[symbol].is_array) set_error("array needs an index");
    return 1; /* Scalar. */
}

static int allocate_temporary(void) {
    if (memory_top >= 4096) {
        set_error("too much temporary storage");
        return -1;
    }
    int slot = memory_top++;
    if (memory_top > function_high_water) function_high_water = memory_top;
    return slot;
}

static void emit_increment(int symbol, int lvalue_kind, int decrement, int prefix) {
    if (symbol < 0) return;
    if (lvalue_kind == 3) {
        int temporary = allocate_temporary();
        emit(BC_DUP, 0);
        emit(BC_DEREF, 0);
        if (prefix) {
            emit(BC_PUSHI, 1);
            emit(decrement ? BC_SUB : BC_ADD, 0);
            emit(BC_STORE, temporary);
            emit(BC_LOAD, temporary);
            emit(BC_STOREPTR, 0);
            emit(BC_LOAD, temporary);
        } else {
            emit(BC_STORE, temporary);
            emit(BC_LOAD, temporary);
            emit(BC_PUSHI, 1);
            emit(decrement ? BC_SUB : BC_ADD, 0);
            emit(BC_STOREPTR, 0);
            emit(BC_LOAD, temporary);
        }
    } else if (lvalue_kind == 2) {
        /*
         * The VM's array store consumes address and value together. Keep the
         * old/new expression result in a hidden cell while the store runs.
         */
        int temporary = allocate_temporary();
        emit(BC_DUP, 0);
        emit(BC_ALOAD, 0);
        if (prefix) {
            emit(BC_PUSHI, 1);
            emit(decrement ? BC_SUB : BC_ADD, 0);
            emit(BC_STORE, temporary);          /* Save the new value. */
            emit(BC_LOAD, temporary);
            emit(BC_ASTORE, 0);
            emit(BC_LOAD, temporary);
        } else {
            emit(BC_STORE, temporary);          /* Save the old value. */
            emit(BC_LOAD, temporary);
            emit(BC_PUSHI, 1);
            emit(decrement ? BC_SUB : BC_ADD, 0);
            emit(BC_ASTORE, 0);
            emit(BC_LOAD, temporary);
        }
    } else {
        emit(symbols[symbol].is_global ? BC_GLOAD : BC_LOAD, symbols[symbol].base);
        if (!prefix) emit(BC_DUP, 0);
        emit(BC_PUSHI, 1);
        emit(decrement ? BC_SUB : BC_ADD, 0);
        if (prefix) emit(BC_DUP, 0);
        emit(symbols[symbol].is_global ? BC_GSTORE : BC_STORE, symbols[symbol].base);
    }
}

static void parse_primary(void) {
    if (tok.kind == T_NUM || tok.kind == T_CHAR) {
        emit_integer(tok.value);
        next_token();
        return;
    }
    if (tok.kind == T_STRING) {
        int offset = add_string(tok.text, tok.length);
        if (offset >= 0) emit(BC_PUSHS, offset);
        next_token();
        return;
    }
    if (accept("(")) {
        parse_expression();
        expect(")");
        return;
    }
    if (tok.kind == T_ID) {
        char name[MAX_NAME + 1];
        int n = tok.length;
        for (int i = 0; i <= n; i++) name[i] = tok.text[i];
        next_token();
        if (streq(tok.text, "(")) {
            parse_call(name);
            return;
        }
        int constant = find_enum_constant(name);
        if (constant >= 0) {
            emit_integer((u64)enum_constants[constant].value);
            return;
        }
        int symbol = require_symbol(name);
        if (symbol < 0) return;
        if (symbols[symbol].is_array && !streq(tok.text, "[")) {
            emit(symbols[symbol].is_global ? BC_GADDR : BC_PUSHADDR, symbols[symbol].base);
            return;
        }
        int kind = parse_lvalue_after_name(symbol);
        if (kind == 2) emit(BC_ALOAD, 0);
        else if (kind == 3) emit(BC_DEREF, 0);
        else emit(symbols[symbol].is_global ? BC_GLOAD : BC_LOAD, symbols[symbol].base);
        return;
    }
    set_error("expected an expression");
    if (tok.kind != T_EOF) next_token();
}

static void parse_postfix(void) {
    if (tok.kind == T_ID) {
        char name[MAX_NAME + 1];
        int n = tok.length;
        for (int i = 0; i <= n; i++) name[i] = tok.text[i];
        next_token();
        if (streq(tok.text, "(")) {
            parse_call(name);
            return;
        }
        int constant = find_enum_constant(name);
        if (constant >= 0) {
            if (streq(tok.text, "++") || streq(tok.text, "--")) set_error("enum constants are not modifiable");
            emit_integer((u64)enum_constants[constant].value);
            return;
        }
        int symbol = require_symbol(name);
        if (symbol < 0) return;
        if (symbols[symbol].is_array && !streq(tok.text, "[")) {
            emit(symbols[symbol].is_global ? BC_GADDR : BC_PUSHADDR, symbols[symbol].base);
            return;
        }
        int kind = parse_lvalue_after_name(symbol);
        if (streq(tok.text, "++") || streq(tok.text, "--")) {
            int decrement = streq(tok.text, "--");
            next_token();
            emit_increment(symbol, kind, decrement, 0);
        } else if (kind == 2) {
            emit(BC_ALOAD, 0);
        } else if (kind == 3) {
            emit(BC_DEREF, 0);
        } else {
            emit(symbols[symbol].is_global ? BC_GLOAD : BC_LOAD, symbols[symbol].base);
        }
        return;
    }
    parse_primary();
}

static void parse_unary(void) {
    if (accept("sizeof")) {
        if (accept("(")) {
            if (is_type_start()) {
                if (parse_type(1) < 0) return;
                int pointers = 0;
                while (accept("*")) pointers++;
                int count = 1;
                if (accept("[")) {
                    if (tok.kind != T_NUM || tok.value < 1 || tok.value > 4096) {
                        set_error("sizeof array bound must be a constant from 1 to 4096"); return;
                    }
                    count = (int)tok.value; next_token(); expect("]");
                }
                expect(")");
                (void)pointers;
                emit_integer((u64)count * 8u);
                return;
            }
            if (tok.kind == T_ID) {
                char name[MAX_NAME + 1]; int n = tok.length;
                for (int i = 0; i <= n; i++) name[i] = tok.text[i];
                next_token(); expect(")");
                int constant = find_enum_constant(name);
                if (constant >= 0) { emit_integer(8); return; }
                int symbol = require_symbol(name);
                if (symbol >= 0) emit_integer((u64)(symbols[symbol].count > 1 ? symbols[symbol].count : 1) * 8u);
                return;
            }
            set_error("Aiden C sizeof currently accepts a type name or object name");
            return;
        }
        if (tok.kind == T_ID) {
            char name[MAX_NAME + 1]; int n = tok.length;
            for (int i = 0; i <= n; i++) name[i] = tok.text[i];
            next_token();
            int symbol = require_symbol(name);
            if (symbol >= 0) emit_integer((u64)(symbols[symbol].count > 1 ? symbols[symbol].count : 1) * 8u);
            return;
        }
        set_error("sizeof requires a type or object name in this Aiden C version");
        return;
    }
    if (accept("*")) { parse_unary(); emit(BC_DEREF, 0); return; }
    if (accept("!")) { parse_unary(); emit(BC_NOT, 0); return; }
    if (accept("~")) { parse_unary(); emit(BC_BITNOT, 0); return; }
    if (accept("-")) { parse_unary(); emit(BC_NEG, 0); return; }
    if (accept("+")) { parse_unary(); return; }
    if (accept("&")) {
        if (tok.kind != T_ID) {
            set_error("address operator requires a scalar variable");
            return;
        }
        char name[MAX_NAME + 1];
        int n = tok.length;
        for (int i = 0; i <= n; i++) name[i] = tok.text[i];
        next_token();
        int constant = find_enum_constant(name);
        if (constant >= 0) {
            set_error("cannot take the address of an enum constant");
            return;
        }
        int symbol = require_symbol(name);
        if (symbol >= 0) emit(symbols[symbol].is_global ? BC_GADDR : BC_PUSHADDR, symbols[symbol].base);
        return;
    }
    if (streq(tok.text, "++") || streq(tok.text, "--")) {
        int decrement = streq(tok.text, "--");
        next_token();
        if (tok.kind != T_ID) {
            set_error("increment/decrement requires a variable");
            return;
        }
        char name[MAX_NAME + 1];
        int n = tok.length;
        for (int i = 0; i <= n; i++) name[i] = tok.text[i];
        next_token();
        int symbol = require_symbol(name);
        if (symbol >= 0) {
            int kind = parse_lvalue_after_name(symbol);
            emit_increment(symbol, kind, decrement, 1);
        }
        return;
    }
    parse_postfix();
}

static void parse_mul(void) {
    parse_unary();
    while (streq(tok.text, "*") || streq(tok.text, "/") || streq(tok.text, "%")) {
        char op = tok.text[0];
        next_token();
        parse_unary();
        emit(op == '*' ? BC_MUL : (op == '/' ? BC_DIV : BC_MOD), 0);
    }
}

static void parse_add(void) {
    parse_mul();
    while (streq(tok.text, "+") || streq(tok.text, "-")) {
        char op = tok.text[0];
        next_token();
        parse_mul();
        emit(op == '+' ? BC_ADD : BC_SUB, 0);
    }
}

static void parse_shift(void) {
    parse_add();
    while (streq(tok.text, "<<") || streq(tok.text, ">>")) {
        int left = streq(tok.text, "<<");
        next_token();
        parse_add();
        emit(left ? BC_SHL : BC_SHR, 0);
    }
}

static void parse_relational(void) {
    parse_shift();
    while (streq(tok.text, "<") || streq(tok.text, ">") ||
           streq(tok.text, "<=") || streq(tok.text, ">=")) {
        char op[3];
        int n = tok.length;
        for (int i = 0; i <= n && i < 3; i++) op[i] = tok.text[i];
        next_token();
        parse_shift();
        emit(streq(op, "<") ? BC_LT : streq(op, ">") ? BC_GT :
             streq(op, "<=") ? BC_LE : BC_GE, 0);
    }
}

static void parse_equality(void) {
    parse_relational();
    while (streq(tok.text, "==") || streq(tok.text, "!=")) {
        int not_equal = streq(tok.text, "!=");
        next_token();
        parse_relational();
        emit(not_equal ? BC_NE : BC_EQ, 0);
    }
}

static void parse_bit_and(void) {
    parse_equality();
    while (streq(tok.text, "&")) {
        next_token();
        parse_equality();
        emit(BC_BITAND, 0);
    }
}

static void parse_bit_xor(void) {
    parse_bit_and();
    while (streq(tok.text, "^")) {
        next_token();
        parse_bit_and();
        emit(BC_BITXOR, 0);
    }
}

static void parse_bit_or(void) {
    parse_bit_xor();
    while (streq(tok.text, "|")) {
        next_token();
        parse_bit_xor();
        emit(BC_BITOR, 0);
    }
}

static void parse_logical_and(void) {
    parse_bit_or();
    while (accept("&&")) {
        int false_jump = emit(BC_JZ, 0);
        parse_bit_or();
        emit(BC_NOT, 0);
        emit(BC_NOT, 0);
        int end_jump = emit(BC_JMP, 0);
        patch(false_jump, code_count);
        emit(BC_PUSHI, 0);
        patch(end_jump, code_count);
    }
}

static void parse_logical_or(void) {
    parse_logical_and();
    while (accept("||")) {
        int evaluate_right = emit(BC_JZ, 0);
        emit(BC_PUSHI, 1);
        int end_jump = emit(BC_JMP, 0);
        patch(evaluate_right, code_count);
        parse_logical_and();
        emit(BC_NOT, 0);
        emit(BC_NOT, 0);
        patch(end_jump, code_count);
    }
}

static int parse_conditional(void) {
    parse_logical_or();
    if (accept("?")) {
        int false_jump = emit(BC_JZ, 0);
        parse_expression();
        expect(":");
        int end_jump = emit(BC_JMP, 0);
        patch(false_jump, code_count);
        parse_conditional();
        patch(end_jump, code_count);
    }
    return 1;
}

static int parse_expression(void) { return parse_conditional(); }

static int is_assignment_operator(const char *text) {
    return streq(text, "=") || streq(text, "+=") || streq(text, "-=") ||
           streq(text, "*=") || streq(text, "/=") || streq(text, "%=") ||
           streq(text, "&=") || streq(text, "|=") || streq(text, "^=") ||
           streq(text, "<<=") || streq(text, ">>=");
}

static int assignment_opcode(const char *text) {
    if (streq(text, "+=")) return BC_ADD;
    if (streq(text, "-=")) return BC_SUB;
    if (streq(text, "*=")) return BC_MUL;
    if (streq(text, "/=")) return BC_DIV;
    if (streq(text, "%=")) return BC_MOD;
    if (streq(text, "&=")) return BC_BITAND;
    if (streq(text, "|=")) return BC_BITOR;
    if (streq(text, "^=")) return BC_BITXOR;
    if (streq(text, "<<=")) return BC_SHL;
    if (streq(text, ">>=")) return BC_SHR;
    return 0;
}

static void parse_declaration(int with_semicolon) {
    if (parse_type(0) < 0) return;
    if (with_semicolon && accept(";")) return; /* Standalone enum definition. */
    do {
        int pointer_depth = 0;
        while (accept("*")) {
            pointer_depth++;
            if (pointer_depth > 2) { set_error("pointer nesting is limited to two levels"); return; }
        }
        if (tok.kind != T_ID) {
            set_error("expected variable name after type");
            return;
        }
        char name[MAX_NAME + 1];
        int n = tok.length;
        for (int i = 0; i <= n; i++) name[i] = tok.text[i];
        next_token();

        int count = 1;
        int is_array = 0;
        if (accept("[")) {
            is_array = 1;
            if (tok.kind != T_NUM) {
                set_error("array length must be an integer constant");
                return;
            }
            if (tok.value < 1 || tok.value > 4096) {
                set_error("array length must be between 1 and 4096");
                return;
            }
            count = (int)tok.value;
            next_token();
            expect("]");
        }

        int symbol = add_symbol(name, count, is_array);
        if (symbol < 0) return;
        symbols[symbol].pointer_depth = pointer_depth;

        if (accept("=")) {
            if (!is_array) {
                parse_expression();
                emit(BC_STORE, symbols[symbol].base);
            } else {
                expect("{");
                int index = 0;
                if (!accept("}")) {
                    do {
                        if (index >= count) {
                            set_error("too many values in array initializer");
                            break;
                        }
                        emit(BC_PUSHI, symbols[symbol].base + index);
                        parse_expression();
                        emit(BC_ASTORE, 0);
                        index++;
                    } while (accept(",") && !streq(tok.text, "}"));
                    expect("}");
                }
            }
        }
    } while (accept(","));

    if (with_semicolon) expect(";");
}

static void parse_simple_statement(int with_semicolon) {
    if (accept("*")) {
        parse_unary();
        if (!is_assignment_operator(tok.text)) {
            set_error("dereferenced pointer statement must assign through the pointer");
            return;
        }
        char op[4]; int len = tok.length;
        for (int i = 0; i <= len && i < 4; i++) op[i] = tok.text[i];
        next_token();
        if (!streq(op, "=")) {
            emit(BC_DUP, 0);
            emit(BC_DEREF, 0);
        }
        parse_expression();
        if (!streq(op, "=")) emit(assignment_opcode(op), 0);
        emit(BC_STOREPTR, 0);
        if (with_semicolon) expect(";");
        return;
    }
    if (tok.kind == T_ID) {
        char name[MAX_NAME + 1];
        int n = tok.length;
        for (int i = 0; i <= n; i++) name[i] = tok.text[i];
        next_token();

        if (streq(tok.text, "(")) {
            parse_call(name);
            emit(BC_POP, 0);
            if (with_semicolon) expect(";");
            return;
        }

        int symbol = require_symbol(name);
        if (symbol < 0) return;
        int kind = parse_lvalue_after_name(symbol);

        if (is_assignment_operator(tok.text)) {
            char op[4];
            int len = tok.length;
            for (int i = 0; i <= len && i < 4; i++) op[i] = tok.text[i];
            next_token();
            if (kind == 1 && !streq(op, "="))
                emit(symbols[symbol].is_global ? BC_GLOAD : BC_LOAD, symbols[symbol].base);
            if ((kind == 2 || kind == 3) && !streq(op, "=")) {
                emit(BC_DUP, 0);
                emit(kind == 2 ? BC_ALOAD : BC_DEREF, 0);
            }
            parse_expression();
            if (!streq(op, "=")) emit(assignment_opcode(op), 0);
            if (kind == 2) emit(BC_ASTORE, 0);
            else if (kind == 3) emit(BC_STOREPTR, 0);
            else emit(symbols[symbol].is_global ? BC_GSTORE : BC_STORE, symbols[symbol].base);
        } else if (streq(tok.text, "++") || streq(tok.text, "--")) {
            int decrement = streq(tok.text, "--");
            next_token();
            emit_increment(symbol, kind, decrement, 1);
            emit(BC_POP, 0);
        } else {
            set_error("expected assignment or increment/decrement after variable");
            return;
        }
        if (with_semicolon) expect(";");
        return;
    }

    if (tok.kind == T_EOF) {
        set_error("expected a statement");
        return;
    }
    parse_expression();
    emit(BC_POP, 0);
    if (with_semicolon) expect(";");
}

static int push_control(void) {
    if (control_count >= MAX_CONTROL) {
        set_error("control-flow nesting is too deep");
        return -1;
    }
    Control *c = &controls[control_count];
    c->kind = CTRL_LOOP;
    c->continue_target = -1;
    c->break_count = 0;
    c->continue_count = 0;
    c->switch_slot = -1;
    c->default_target = -1;
    c->case_count = 0;
    return control_count++;
}

static void patch_control(int index, int end, int continue_target) {
    if (index < 0 || index >= control_count) return;
    Control *c = &controls[index];
    for (int i = 0; i < c->break_count; i++) patch(c->breaks[i], end);
    for (int i = 0; i < c->continue_count; i++) patch(c->continues[i], continue_target);
    control_count = index;
}

static int current_loop(void) {
    for (int i = control_count - 1; i >= 0; i--) {
        if (controls[i].kind == CTRL_LOOP) return i;
    }
    return -1;
}

static int current_breakable(void) {
    for (int i = control_count - 1; i >= 0; i--)
        if (controls[i].kind == CTRL_LOOP || controls[i].kind == CTRL_SWITCH) return i;
    return -1;
}

static int current_switch(void) {
    for (int i = control_count - 1; i >= 0; i--)
        if (controls[i].kind == CTRL_SWITCH) return i;
    return -1;
}

static void parse_case_label(int is_default) {
    int index = current_switch();
    if (index < 0) { set_error(is_default ? "default label outside switch" : "case label outside switch"); return; }
    Control *control = &controls[index];
    if (is_default) {
        if (control->default_target >= 0) { set_error("duplicate default label"); return; }
        expect(":");
        control->default_target = code_count;
    } else {
        i64 value = 0;
        if (!parse_enum_constant_value(&value)) return;
        expect(":");
        for (int i = 0; i < control->case_count; i++)
            if (control->cases[i].value == value) { set_error("duplicate case value"); return; }
        if (control->case_count >= MAX_SWITCH_CASES) { set_error("switch has more than 64 case labels"); return; }
        control->cases[control->case_count].value = value;
        control->cases[control->case_count].target = code_count;
        control->case_count++;
    }
    if (is_type_start()) parse_declaration(1);
    else parse_statement();
}

static void parse_break_or_continue(int is_continue) {
    expect(";");
    int index = is_continue ? current_loop() : current_breakable();
    if (index < 0) {
        set_error(is_continue ? "continue used outside a loop" : "break used outside a loop");
        return;
    }
    Control *c = &controls[index];
    if (is_continue) {
        if (c->continue_count >= MAX_PATCHES) {
            set_error("too many continue statements in one loop");
            return;
        }
        c->continues[c->continue_count++] = emit(BC_JMP, 0);
    } else {
        if (c->break_count >= MAX_PATCHES) {
            set_error("too many break statements in one loop");
            return;
        }
        c->breaks[c->break_count++] = emit(BC_JMP, 0);
    }
}

static void parse_block(void) {
    expect("{");
    int saved_symbols = symbol_count;
    int saved_memory = memory_top;
    enter_scope();

    while (tok.kind != T_EOF && !streq(tok.text, "}") && !failed) {
        if (is_type_start()) parse_declaration(1);
        else parse_statement();
    }
    expect("}");
    leave_scope(saved_symbols, saved_memory);
}

static void parse_statement(void) {
    if (failed) return;
    if (streq(tok.text, "{")) { parse_block(); return; }
    if (accept(";")) return;

    if (accept("if")) {
        expect("(");
        parse_expression();
        expect(")");
        int false_jump = emit(BC_JZ, 0);
        parse_statement();
        if (accept("else")) {
            int end_jump = emit(BC_JMP, 0);
            patch(false_jump, code_count);
            parse_statement();
            patch(end_jump, code_count);
        } else {
            patch(false_jump, code_count);
        }
        return;
    }

    if (accept("switch")) {
        expect("(");
        parse_expression();
        expect(")");
        int value_slot = allocate_temporary();
        emit(BC_STORE, value_slot);
        int control = push_control();
        if (control < 0) return;
        controls[control].kind = CTRL_SWITCH;
        controls[control].switch_slot = value_slot;
        int dispatch_jump = emit(BC_JMP, 0); /* Do not fall into the first case. */
        parse_statement();
        int skip_dispatch = emit(BC_JMP, 0);
        patch(dispatch_jump, code_count);
        for (int i = 0; i < controls[control].case_count; i++) {
            emit(BC_LOAD, value_slot);
            emit_integer((u64)controls[control].cases[i].value);
            emit(BC_EQ, 0);
            int next_case = emit(BC_JZ, 0);
            emit(BC_JMP, controls[control].cases[i].target);
            patch(next_case, code_count);
        }
        if (controls[control].default_target >= 0)
            emit(BC_JMP, controls[control].default_target);
        int end = code_count;
        patch(skip_dispatch, end);
        patch_control(control, end, -1);
        return;
    }

    if (accept("case")) { parse_case_label(0); return; }
    if (accept("default")) { parse_case_label(1); return; }

    if (accept("while")) {
        int condition = code_count;
        expect("(");
        parse_expression();
        expect(")");
        int end_jump = emit(BC_JZ, 0);
        int control = push_control();
        parse_statement();
        emit(BC_JMP, condition);
        int end = code_count;
        patch(end_jump, end);
        patch_control(control, end, condition);
        return;
    }

    if (accept("do")) {
        int body = code_count;
        int control = push_control();
        parse_statement();
        int condition = code_count;
        expect("while");
        expect("(");
        parse_expression();
        expect(")");
        expect(";");
        int end_jump = emit(BC_JZ, 0);
        emit(BC_JMP, body);
        int end = code_count;
        patch(end_jump, end);
        patch_control(control, end, condition);
        return;
    }

    if (accept("for")) {
        int saved_symbols = symbol_count;
        int saved_memory = memory_top;
        enter_scope();
        expect("(");
        if (is_type_start()) parse_declaration(1);
        else if (!accept(";")) parse_simple_statement(1);

        int condition = code_count;
        if (!streq(tok.text, ";")) parse_expression();
        else emit(BC_PUSHI, 1);
        expect(";");
        int end_jump = emit(BC_JZ, 0);
        int body_jump = emit(BC_JMP, 0);

        int increment = code_count;
        if (!streq(tok.text, ")")) parse_simple_statement(0);
        expect(")");
        emit(BC_JMP, condition);

        int body = code_count;
        patch(body_jump, body);
        int control = push_control();
        parse_statement();
        emit(BC_JMP, increment);
        int end = code_count;
        patch(end_jump, end);
        patch_control(control, end, increment);
        leave_scope(saved_symbols, saved_memory);
        return;
    }

    if (accept("break")) { parse_break_or_continue(0); return; }
    if (accept("continue")) { parse_break_or_continue(1); return; }

    if (accept("return")) {
        if (streq(tok.text, ";")) emit(BC_PUSHI, 0);
        else parse_expression();
        expect(";");
        emit(BC_RET, 0);
        return;
    }

    parse_simple_statement(1);
}

static int global_const_expression(i64 *value);
static int global_const_unary(i64 *value);

static int global_const_primary(i64 *value) {
    if (tok.kind == T_NUM || tok.kind == T_CHAR) {
        u64 raw = tok.value;
        next_token();
        if (raw > 0x7FFFFFFFFFFFFFFFULL) {
            set_error("global initializer literal exceeds signed 64-bit range");
            return 0;
        }
        *value = (i64)raw;
        return 1;
    }
    if (tok.kind == T_ID) {
        int e = find_enum_constant(tok.text);
        if (e >= 0) { *value = enum_constants[e].value; next_token(); return 1; }
    }
    if (accept("(")) {
        if (!global_const_expression(value)) return 0;
        expect(")");
        return !failed;
    }
    set_error("global initializer must be a constant expression");
    return 0;
}

static int global_const_unary(i64 *value) {
    if (accept("+")) return global_const_unary(value);
    if (accept("-")) {
        if (!global_const_unary(value)) return 0;
        *value = (i64)(0u - (u64)*value);
        return 1;
    }
    if (accept("~")) {
        if (!global_const_unary(value)) return 0;
        *value = (i64)~(u64)*value;
        return 1;
    }
    if (accept("!")) {
        if (!global_const_unary(value)) return 0;
        *value = !*value;
        return 1;
    }
    return global_const_primary(value);
}

static int global_const_mul(i64 *value) {
    if (!global_const_unary(value)) return 0;
    while (streq(tok.text, "*") || streq(tok.text, "/") || streq(tok.text, "%")) {
        char op = tok.text[0]; next_token();
        i64 right;
        if (!global_const_unary(&right)) return 0;
        if ((op == '/' || op == '%') && right == 0) { set_error("division by zero in global initializer"); return 0; }
        if (op == '*') *value = (i64)((u64)*value * (u64)right);
        else if (*value == (-9223372036854775807LL - 1LL) && right == -1) {
            if (op == '/') *value = (-9223372036854775807LL - 1LL);
            else *value = 0;
        } else if (op == '/') *value /= right;
        else *value %= right;
    }
    return 1;
}

static int global_const_add(i64 *value) {
    if (!global_const_mul(value)) return 0;
    while (streq(tok.text, "+") || streq(tok.text, "-")) {
        int add = streq(tok.text, "+"); next_token();
        i64 right; if (!global_const_mul(&right)) return 0;
        *value = (i64)(add ? (u64)*value + (u64)right : (u64)*value - (u64)right);
    }
    return 1;
}

static int global_const_shift(i64 *value) {
    if (!global_const_add(value)) return 0;
    while (streq(tok.text, "<<") || streq(tok.text, ">>")) {
        int left = streq(tok.text, "<<"); next_token();
        i64 right; if (!global_const_add(&right)) return 0;
        if (right < 0 || right >= 64) { set_error("invalid shift count in global initializer"); return 0; }
        if (left) *value = (i64)((u64)*value << (unsigned)right);
        else if (right == 0) { /* Keep the exact input. */ }
        else if (*value >= 0) *value = (i64)((u64)*value >> (unsigned)right);
        else *value = (i64)(((u64)*value >> (unsigned)right) | (~(u64)0 << (64u - (unsigned)right)));
    }
    return 1;
}

static int global_const_rel(i64 *value) {
    if (!global_const_shift(value)) return 0;
    while (streq(tok.text, "<") || streq(tok.text, ">") || streq(tok.text, "<=") || streq(tok.text, ">=")) {
        char op[3]; int n = tok.length; for (int i=0;i<=n;i++) op[i]=tok.text[i]; next_token();
        i64 right; if (!global_const_shift(&right)) return 0;
        *value = streq(op,"<") ? (*value < right) : streq(op,">") ? (*value > right) :
                 streq(op,"<=") ? (*value <= right) : (*value >= right);
    }
    return 1;
}

static int global_const_eq(i64 *value) {
    if (!global_const_rel(value)) return 0;
    while (streq(tok.text, "==") || streq(tok.text, "!=")) {
        int equal = streq(tok.text,"=="); next_token();
        i64 right; if (!global_const_rel(&right)) return 0;
        *value = equal ? (*value == right) : (*value != right);
    }
    return 1;
}

static int global_const_bitand(i64 *value) {
    if (!global_const_eq(value)) return 0;
    while (streq(tok.text,"&")) { next_token(); i64 r; if(!global_const_eq(&r))return 0; *value=(i64)((u64)*value&(u64)r); }
    return 1;
}
static int global_const_xor(i64 *value) {
    if (!global_const_bitand(value)) return 0;
    while (streq(tok.text,"^")) { next_token(); i64 r; if(!global_const_bitand(&r))return 0; *value=(i64)((u64)*value^(u64)r); }
    return 1;
}
static int global_const_bitor(i64 *value) {
    if (!global_const_xor(value)) return 0;
    while (streq(tok.text,"|")) { next_token(); i64 r; if(!global_const_xor(&r))return 0; *value=(i64)((u64)*value|(u64)r); }
    return 1;
}
static int global_const_logical_and(i64 *value) {
    if (!global_const_bitor(value)) return 0;
    while (streq(tok.text,"&&")) { next_token(); i64 r; if(!global_const_bitor(&r))return 0; *value=(*value!=0 && r!=0); }
    return 1;
}
static int global_const_logical_or(i64 *value) {
    if (!global_const_logical_and(value)) return 0;
    while (streq(tok.text,"||")) { next_token(); i64 r; if(!global_const_logical_and(&r))return 0; *value=(*value!=0 || r!=0); }
    return 1;
}
static int global_const_expression(i64 *value) {
    if (!global_const_logical_or(value)) return 0;
    if (accept("?")) {
        i64 yes, no;
        if (!global_const_expression(&yes)) return 0;
        expect(":");
        if (!global_const_expression(&no)) return 0;
        *value = *value ? yes : no;
    }
    return !failed;
}

static void parse_global_declarator(const char *name, int pointer_depth) {
    int count = 1;
    int is_array = 0;
    if (accept("[")) {
        is_array = 1;
        if (tok.kind != T_NUM || tok.value < 1 || tok.value > MAX_GLOBAL_CELLS) {
            set_error("global array size must be between 1 and 256"); return;
        }
        count = (int)tok.value;
        next_token();
        expect("]");
    }
    int index = add_global_symbol(name, count, is_array, pointer_depth);
    if (index < 0) return;
    if (accept("=")) {
        if (is_array) {
            expect("{");
            int cell = 0;
            if (!accept("}")) {
                do {
                    if (cell >= count) { set_error("too many global array initializer values"); return; }
                    i64 value = 0;
                    if (!global_const_expression(&value)) return;
                    global_values[global_symbols[index].base + cell++] = value;
                } while (accept(",") && !streq(tok.text, "}"));
                expect("}");
            }
        } else {
            i64 value = 0;
            if (!global_const_expression(&value)) return;
            global_values[global_symbols[index].base] = value;
        }
    }
}

static void parse_global_declaration(const char *first_name, int first_pointer_depth) {
    parse_global_declarator(first_name, first_pointer_depth);
    while (accept(",") && !failed) {
        int pointers = 0;
        while (accept("*")) {
            if (++pointers > 2) { set_error("pointer nesting is limited to two levels"); return; }
        }
        if (tok.kind != T_ID) { set_error("expected global variable name"); return; }
        char name[MAX_NAME + 1];
        int n = tok.length;
        for (int i = 0; i <= n; ++i) name[i] = tok.text[i];
        next_token();
        parse_global_declarator(name, pointers);
    }
    expect(";");
}

static void write_hex(char *out, usize cap, usize *pos, u8 byte) {
    static const char hex[] = "0123456789ABCDEF";
    if (*pos + 2 >= cap) {
        set_error("executable output buffer is too small");
        return;
    }
    out[(*pos)++] = hex[(byte >> 4) & 15];
    out[(*pos)++] = hex[byte & 15];
}

static int parse_parameters(Parameter params[MAX_PARAMS], int *parameter_count) {
    *parameter_count = 0;
    expect("(");
    if (accept(")")) return 1;
    if (accept("void")) {
        expect(")");
        return 1;
    }
    do {
        if (parse_type(0) < 0) return 0;
        int pointer_depth = 0;
        while (accept("*")) {
            pointer_depth++;
            if (pointer_depth > 2) { set_error("pointer nesting is limited to two levels"); return 0; }
        }
        if (*parameter_count >= MAX_PARAMS) {
            set_error("a function may have at most 16 parameters");
            return 0;
        }
        Parameter *param = &params[*parameter_count];
        param->name[0] = 0;
        param->has_name = 0;
        param->pointer_depth = pointer_depth;
        if (tok.kind == T_ID) {
            int n = tok.length;
            for (int i = 0; i <= n; i++) param->name[i] = tok.text[i];
            param->has_name = 1;
            next_token();
        }
        if (accept("[")) {
            set_error("array parameters are not supported yet; pass scalar values");
            return 0;
        }
        (*parameter_count)++;
    } while (accept(",") && !failed);
    expect(")");
    return !failed;
}

static void begin_function(int function_index, Parameter params[MAX_PARAMS], int argc) {
    symbol_count = 0;
    memory_top = 0;
    scope_depth = 1; /* Parameters share the function body's outer block scope. */
    control_count = 0;
    function_high_water = 0;

    for (int i = 0; i < global_symbol_count; ++i) {
        if (symbol_count >= MAX_SYMBOLS) { set_error("global symbol table exceeds function symbol capacity"); return; }
        symbols[symbol_count] = global_symbols[i];
        ++symbol_count;
    }

    for (int i = 0; i < argc && !failed; i++) {
        if (!params[i].has_name) {
            set_error("function definitions require a name for every parameter");
            return;
        }
        int slot = add_symbol(params[i].name, 1, 0);
        if (slot < 0) return;
        symbols[slot].pointer_depth = params[i].pointer_depth;
        if (symbols[slot].base != i) {
            set_error("internal parameter layout error");
            return;
        }
    }

    scope_depth = 0;
    int enter_instruction = emit(BC_ENTER, 0);
    functions[function_index].code_start = code_count - 1;
    functions[function_index].defined = 1;
    patch_function_calls(function_index);

    parse_block();
    if (!failed) {
        /* Falling off the end returns zero, matching the old main behavior. */
        emit(BC_PUSHI, 0);
        emit(BC_RET, 0);
    }

    functions[function_index].local_count = function_high_water;
    if (functions[function_index].local_count < argc) {
        functions[function_index].local_count = argc;
    }
    if (functions[function_index].local_count > 4096) {
        set_error("function needs more than 4096 local storage cells");
    }
    patch(enter_instruction, functions[function_index].local_count);
}

int c_compile(const char *src, char *image, usize cap) {
    code_count = 0;
    strings_count = 0;
    symbol_count = 0;
    global_symbol_count = 0;
    global_cell_count = 0;
    for (int i = 0; i < MAX_GLOBAL_CELLS; ++i) global_values[i] = 0;
    typedef_count = 0;
    enum_constant_count = 0;
    memory_top = 0;
    scope_depth = 0;
    control_count = 0;
    function_high_water = 0;
    function_count = 0;
    user_call_patch_count = 0;
    main_function_index = -1;
    source_ptr = src;
    source_line = 1;
    source_column = 1;
    previous_token_line = 1;
    previous_token_column = 1;
    tok.kind = T_EOF;
    tok.text[0] = 0;
    tok.length = 0;
    tok.line = 1;
    tok.column = 1;
    failed = 0;
    error_text[0] = 0;

    if (!src || !image || cap < 32) {
        set_error("invalid compiler input or output buffer");
        return -1;
    }

    usize source_length = 0;
    while (source_length <= MAX_SOURCE_BYTES && src[source_length]) ++source_length;
    if (source_length > MAX_SOURCE_BYTES) {
        set_error("source exceeds the 8192-byte Aiden C source limit");
        return -1;
    }
    preprocess_source(src);
    if (failed) return -1;
    source_ptr = preprocessed_source;
    source_line = 1;
    source_column = 1;
    next_token();
    /* Execution starts with a jump over helper functions to main. */
    int main_jump = emit(BC_JMP, 0);

    while (tok.kind != T_EOF && !failed) {
        if (accept("typedef")) { parse_typedef_declaration(); continue; }
        if (!is_type_start()) {
            set_error("expected a global declaration, function prototype, or function definition");
            break;
        }

        int return_type = parse_type(1);
        if (return_type < 0) break;
        int pointer_depth = 0;
        while (accept("*")) {
            if (++pointer_depth > 2) { set_error("pointer nesting is limited to two levels"); break; }
        }
        if (failed) break;

        if (accept(";")) continue; /* Standalone enum declaration. */
        if (tok.kind != T_ID) { set_error("expected a global or function name"); break; }
        char declarator_name[MAX_NAME + 1];
        int name_length = tok.length;
        for (int i = 0; i <= name_length; ++i) declarator_name[i] = tok.text[i];
        next_token();

        if (streq(tok.text, "(")) {
            if (pointer_depth) { set_error("function pointer return types are not supported"); break; }
            Parameter params[MAX_PARAMS];
            int argc = 0;
            if (!parse_parameters(params, &argc)) break;
            int function_index = declare_function(declarator_name, return_type, params, argc);
            if (function_index < 0) break;
            if (accept(";")) continue; /* Prototype. */
            if (!streq(tok.text, "{")) { set_expected("; or function body"); break; }
            if (functions[function_index].defined) {
                set_error("function is defined more than once");
                break;
            }
            if (streq(declarator_name, "main")) {
                if (argc != 0) { set_error("main must not have parameters"); break; }
                main_function_index = function_index;
            }
            begin_function(function_index, params, argc);
        } else {
            if (return_type == 0) { set_error("global variables cannot have void type"); break; }
            parse_global_declaration(declarator_name, pointer_depth);
        }
    }

    if (!failed && main_function_index < 0) {
        set_error("program must define int main(void) or void main(void)");
    }
    if (!failed) {
        patch(main_jump, functions[main_function_index].code_start);
        for (int i = 0; i < user_call_patch_count; i++) {
            if (user_call_patches[i].function_index >= 0) {
                set_error("called function was declared but never defined");
                break;
            }
        }
    }
    if (failed) return -1;

    usize p = 0;
    if (cap < 10) {
        set_error("executable output buffer is too small");
        return -1;
    }
    image[p++] = 'A'; image[p++] = 'E'; image[p++] = 'X'; image[p++] = '6';

    /* Append fixed-width initialized global cells after ordinary string data. */
    if (strings_count + global_cell_count * 8 > MAX_STRINGS) {
        set_error("global initializers exceed the compiler data table");
        return -1;
    }
    for (int i = 0; i < global_cell_count; ++i) {
        u64 bits = (u64)global_values[i];
        for (int byte = 0; byte < 8; ++byte) strings[strings_count++] = (char)(bits >> (byte * 8));
    }

    u16 cc = (u16)code_count;
    u16 ss = (u16)strings_count;
    u16 gc = (u16)global_cell_count;
    u8 header[8] = { (u8)(cc >> 8), (u8)cc, (u8)(ss >> 8), (u8)ss,
                     (u8)(gc >> 8), (u8)gc, 0, 0 };
    for (int i = 0; i < 8; i++) write_hex(image, cap, &p, header[i]);
    for (int i = 0; i < code_count; i++) {
        u32 arg = (u32)code[i].arg;
        write_hex(image, cap, &p, code[i].op);
        write_hex(image, cap, &p, (u8)(arg & 255));
        write_hex(image, cap, &p, (u8)((arg >> 8) & 255));
        write_hex(image, cap, &p, (u8)((arg >> 16) & 255));
        write_hex(image, cap, &p, (u8)((arg >> 24) & 255));
    }
    for (int i = 0; i < strings_count; i++) write_hex(image, cap, &p, (u8)strings[i]);
    if (failed) return -1;
    image[p] = 0;
    return (int)p;
}

const char *c_compile_error(void) {
    return error_text[0] ? error_text : "compile failed";
}
