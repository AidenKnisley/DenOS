#include "completion.h"

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}
static usize text_len(const char *s) {
    usize n = 0;
    while (s && s[n]) n++;
    return n;
}
static int starts_with(const char *word, const char *prefix, usize n) {
    for (usize i = 0; i < n; ++i)
        if (!word[i] || word[i] != prefix[i]) return 0;
    return word[n] != 0;
}
static int already_added(const CCompletionItem *items, int count,
                         const char *word) {
    for (int i = 0; i < count; ++i) {
        usize j = 0;
        while (items[i].text[j] && word[j] && items[i].text[j] == word[j]) j++;
        if (!items[i].text[j] && !word[j]) return 1;
    }
    return 0;
}
static int add_item(CCompletionItem *items, int count, int capacity,
                    const char *word, u8 kind, const char *prefix, usize pn) {
    usize n = text_len(word);
    if (!n || n >= CCOMPLETE_WORD_CAP || n <= pn ||
        !starts_with(word, prefix, pn) || count >= capacity ||
        already_added(items, count, word)) return count;
    for (usize i = 0; i <= n; ++i) items[count].text[i] = word[i];
    items[count].kind = kind;
    return count + 1;
}

int c_completion_collect(const char *source, usize source_length,
                         const char *prefix, CCompletionItem *items,
                         int capacity) {
    static const char *keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default",
        "do", "double", "else", "enum", "extern", "float", "for", "goto",
        "if", "inline", "int", "long", "register", "restrict", "return",
        "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
        "union", "unsigned", "void", "volatile", "while", "_Bool"
    };
    static const char *types[] = {
        "size_t", "ptrdiff_t", "ssize_t", "u8", "u16", "u32", "u64",
        "i8", "i16", "i32", "i64", "usize", "isize", "uintptr_t",
        "intptr_t", "bool"
    };
    /* Keep this list synchronized with builtin_id() in compiler.c. */
    static const char *functions[] = {
        "printf", "puts", "putchar", "scanf", "getchar", "strlen",
        "strcmp", "strncmp", "strcpy", "strncpy", "strcat", "strchr",
        "strrchr", "strstr", "abs", "labs", "atoi", "isdigit",
        "isalpha", "isspace", "toupper", "tolower", "isalnum", "isupper",
        "islower", "isxdigit", "iscntrl", "isprint", "isgraph", "ispunct"
    };
    if (!items || capacity <= 0 || !prefix) return 0;
    if (capacity > CCOMPLETE_MAX_ITEMS) capacity = CCOMPLETE_MAX_ITEMS;
    usize pn = text_len(prefix);
    if (pn < 2 || pn >= CCOMPLETE_WORD_CAP) return 0;

    int count = 0;

    /* Offer identifiers from this buffer first. This makes local names and
       project-specific APIs more useful than a fixed list of language words. */
    usize p = 0;
    while (source && p < source_length && count < capacity) {
        char c = source[p];
        if (c == '/' && p + 1 < source_length && source[p + 1] == '/') {
            p += 2;
            while (p < source_length && source[p] != '\n') p++;
            continue;
        }
        if (c == '/' && p + 1 < source_length && source[p + 1] == '*') {
            p += 2;
            while (p + 1 < source_length && !(source[p] == '*' && source[p + 1] == '/')) p++;
            if (p + 1 < source_length) p += 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            char quote = c;
            p++;
            while (p < source_length) {
                if (source[p] == '\\' && p + 1 < source_length) { p += 2; continue; }
                if (source[p++] == quote) break;
            }
            continue;
        }
        if (!is_ident_start(c)) { p++; continue; }
        char word[CCOMPLETE_WORD_CAP];
        usize n = 0;
        while (p < source_length && is_ident_char(source[p])) {
            if (n + 1 < sizeof(word)) word[n++] = source[p];
            p++;
        }
        word[n] = 0;
        if (n > pn && n < sizeof(word) && starts_with(word, prefix, pn)) {
            u8 kind = CCOMPLETE_SYMBOL;
            usize look = p;
            while (look < source_length && (source[look] == ' ' || source[look] == '\t' ||
                   source[look] == '\r' || source[look] == '\n')) look++;
            if (look < source_length && source[look] == '(') kind = CCOMPLETE_FUNCTION;
            count = add_item(items, count, capacity, word, kind, prefix, pn);
        }
    }

    /* Add compiler-provided functions before generic words. */
    for (usize i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i)
        count = add_item(items, count, capacity, functions[i], CCOMPLETE_FUNCTION, prefix, pn);
    for (usize i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
        count = add_item(items, count, capacity, types[i], CCOMPLETE_TYPE, prefix, pn);
    for (usize i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i)
        count = add_item(items, count, capacity, keywords[i], CCOMPLETE_KEYWORD, prefix, pn);
    return count;
}
