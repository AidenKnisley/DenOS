#include "path.h"

static usize path_length(const char *text, usize limit) {
    usize n = 0;
    if (!text) return 0;
    while (n < limit && text[n]) n++;
    return n;
}

/* A leaf name is intentionally strict so shell path operations have one safe component. */
int fs_name_valid(const char *name) {
    usize length = 0;
    if (!name || !name[0]) return 0;
    while (name[length]) {
        unsigned char c = (unsigned char)name[length];
        if (c < 33 || c > 126 || c == '/' || c == '\\' || c == ':') return 0;
        length++;
        if (length >= 64) return 0;
    }
    if ((length == 1 && name[0] == '.') ||
        (length == 2 && name[0] == '.' && name[1] == '.')) return 0;
    return 1;
}

int fs_path_valid(const char *path) {
    usize length, component = 0;
    if (!path) return 0;
    length = path_length(path, 64);
    if (length == 0 || length >= 64 || path[0] == '/' || path[length - 1] == '/') return 0;
    for (usize i = 0; i <= length; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c == '/' || c == 0) {
            usize size = i - component;
            if (size == 0) return 0;
            if (size == 1 && path[component] == '.') return 0;
            if (size == 2 && path[component] == '.' && path[component + 1] == '.') return 0;
            component = i + 1;
            continue;
        }
        if (c < 32 || c == 127 || c == '\\' || c == ':') return 0;
    }
    return 1;
}

static int append_component(char *output, usize *used, usize capacity,
                             const char *component, usize length) {
    if (!length || (length == 1 && component[0] == '.')) return 0;
    if (length == 2 && component[0] == '.' && component[1] == '.') {
        if (!*used) return -1;
        while (*used && output[*used - 1] != '/') (*used)--;
        if (*used) (*used)--;
        output[*used] = 0;
        return 0;
    }
    if (*used && *used + 1 < capacity) output[(*used)++] = '/';
    if (*used + length >= capacity) return -1;
    for (usize i = 0; i < length; i++) {
        unsigned char c = (unsigned char)component[i];
        if (c < 32 || c == 127 || c == '\\' || c == ':') return -1;
        output[(*used)++] = (char)c;
    }
    output[*used] = 0;
    return 0;
}

/* Normalize absolute and relative paths into the on-disk, slash-separated form.
 * Examples: base="home/user", input="../Documents" -> "home/Documents";
 * input="/home/user" is rooted at the filesystem root. */
int fs_path_normalize(const char *base, const char *input,
                      char *output, usize capacity) {
    usize used = 0;
    const char *cursor;
    if (!input || !output || capacity < 2) return -1;
    output[0] = 0;
    if (input[0] != '/') {
        if (base && base[0]) {
            cursor = base;
            while (*cursor) {
                const char *start = cursor;
                while (*cursor && *cursor != '/') cursor++;
                if (append_component(output, &used, capacity, start,
                                     (usize)(cursor - start)) < 0) return -1;
                if (*cursor == '/') cursor++;
            }
        }
    }
    cursor = input;
    while (*cursor) {
        const char *start;
        while (*cursor == '/') cursor++;
        if (!*cursor) break;
        start = cursor;
        while (*cursor && *cursor != '/') cursor++;
        if (append_component(output, &used, capacity, start,
                             (usize)(cursor - start)) < 0) return -1;
    }
    if (used && !fs_path_valid(output)) return -1;
    return 0;
}

int fs_path_parent(const char *path, char *output, usize capacity) {
    usize length;
    if (!path || !output || capacity == 0 || !fs_path_valid(path)) return -1;
    length = path_length(path, 64);
    while (length && path[length - 1] != '/') length--;
    if (length) length--;
    if (length + 1 > capacity) return -1;
    for (usize i = 0; i < length; i++) output[i] = path[i];
    output[length] = 0;
    return 0;
}

const char *fs_path_leaf(const char *path) {
    const char *leaf;
    if (!path) return "";
    leaf = path;
    for (const char *cursor = path; *cursor; cursor++)
        if (*cursor == '/') leaf = cursor + 1;
    return leaf;
}

int fs_name_is_hidden(const char *name) {
    const char *leaf = fs_path_leaf(name);
    return leaf && leaf[0] == '.' && leaf[1] != '\0';
}

const char *fs_name_extension(const char *name) {
    const char *dot = 0;
    const char *leaf = fs_path_leaf(name);
    if (!leaf) return "";
    for (const char *cursor = leaf; *cursor; cursor++)
        if (*cursor == '.') dot = cursor;
    return dot && dot[1] ? dot + 1 : "";
}
