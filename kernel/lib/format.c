#include "kformat.h"

struct output_buffer {
    char *data;
    usize capacity;
    usize length;
};

static void output_char(struct output_buffer *out, char value) {
    if (out->capacity && out->length + 1 < out->capacity)
        out->data[out->length] = value;
    if (out->length < (usize)0x7FFFFFFF) out->length++;
}

static void output_repeat(struct output_buffer *out, char value, usize count) {
    while (count--) output_char(out, value);
}

static void output_string(struct output_buffer *out, const char *text,
                          usize width, int left) {
    usize length = 0;
    if (!text) text = "(null)";
    while (text[length]) length++;
    if (!left && width > length) output_repeat(out, ' ', width - length);
    for (usize i = 0; i < length; i++) output_char(out, text[i]);
    if (left && width > length) output_repeat(out, ' ', width - length);
}

static usize unsigned_digits(unsigned long long value, unsigned base,
                             int uppercase, char *buffer) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    usize length = 0;
    do {
        buffer[length++] = digits[value % base];
        value /= base;
    } while (value && length < 32);
    return length;
}

static void output_number(struct output_buffer *out, unsigned long long value,
                          int negative, unsigned base, int uppercase,
                          usize width, int left, int zero_pad, char sign) {
    char digits[32];
    usize length = unsigned_digits(value, base, uppercase, digits);
    usize prefix = (negative || sign) ? 1u : 0u;
    usize padding = width > length + prefix ? width - length - prefix : 0;

    if (!left && !zero_pad) output_repeat(out, ' ', padding);
    if (negative) output_char(out, '-');
    else if (sign) output_char(out, sign);
    if (!left && zero_pad) output_repeat(out, '0', padding);
    while (length) output_char(out, digits[--length]);
    if (left) output_repeat(out, ' ', padding);
}

static void output_pointer(struct output_buffer *out, unsigned long long value,
                           usize width, int left, int zero_pad) {
    char digits[32];
    usize length = unsigned_digits(value, 16, 0, digits);
    usize total = length + 2u;
    usize padding = width > total ? width - total : 0;
    if (!left && !zero_pad) output_repeat(out, ' ', padding);
    output_char(out, '0');
    output_char(out, 'x');
    if (!left && zero_pad) output_repeat(out, '0', padding);
    while (length) output_char(out, digits[--length]);
    if (left) output_repeat(out, ' ', padding);
}

int kvsnprintf(char *buffer, usize capacity, const char *format, va_list args) {
    struct output_buffer out;
    out.data = buffer;
    out.capacity = buffer ? capacity : 0;
    out.length = 0;
    if (!format) format = "(null format)";

    while (*format) {
        if (*format != '%') {
            output_char(&out, *format++);
            continue;
        }
        format++;
        if (*format == '%') {
            output_char(&out, '%');
            format++;
            continue;
        }

        int left = 0;
        int zero_pad = 0;
        int show_sign = 0;
        int parsing = 1;
        while (parsing) {
            switch (*format) {
                case '-': left = 1; format++; break;
                case '0': zero_pad = 1; format++; break;
                case '+': show_sign = 1; format++; break;
                default: parsing = 0; break;
            }
        }

        usize width = 0;
        while (*format >= '0' && *format <= '9') {
            if (width < 128) width = width * 10 + (usize)(*format - '0');
            format++;
        }
        if (width > 128) width = 128;

        enum { LEN_DEFAULT, LEN_LONG, LEN_LLONG, LEN_SIZE } length = LEN_DEFAULT;
        if (*format == 'l') {
            format++;
            length = LEN_LONG;
            if (*format == 'l') { format++; length = LEN_LLONG; }
        } else if (*format == 'z') {
            format++;
            length = LEN_SIZE;
        }

        char conversion = *format ? *format++ : '\0';
        switch (conversion) {
            case 's':
                output_string(&out, va_arg(args, const char *), width, left);
                break;
            case 'c':
                if (!left && width > 1) output_repeat(&out, ' ', width - 1);
                output_char(&out, (char)va_arg(args, int));
                if (left && width > 1) output_repeat(&out, ' ', width - 1);
                break;
            case 'd':
            case 'i': {
                long long signed_value;
                if (length == LEN_LLONG) signed_value = va_arg(args, long long);
                else if (length == LEN_LONG) signed_value = va_arg(args, long);
                else if (length == LEN_SIZE) signed_value = (long long)va_arg(args, usize);
                else signed_value = va_arg(args, int);
                int negative = signed_value < 0;
                unsigned long long magnitude = negative
                    ? (unsigned long long)(-(signed_value + 1)) + 1u
                    : (unsigned long long)signed_value;
                output_number(&out, magnitude, negative, 10, 0, width,
                              left, zero_pad, show_sign ? '+' : 0);
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'p': {
                unsigned long long value;
                unsigned base = conversion == 'u' ? 10u : 16u;
                if (conversion == 'p') {
                    value = (unsigned long long)(usize)va_arg(args, void *);
                    output_pointer(&out, value, width, left, zero_pad);
                } else if (length == LEN_LLONG) value = va_arg(args, unsigned long long);
                else if (length == LEN_LONG) value = va_arg(args, unsigned long);
                else if (length == LEN_SIZE) value = va_arg(args, usize);
                else value = va_arg(args, unsigned int);
                if (conversion != 'p')
                    output_number(&out, value, 0, base, conversion == 'X', width,
                                  left, zero_pad, 0);
                break;
            }
            case '\0':
                output_char(&out, '%');
                break;
            default:
                output_char(&out, '%');
                output_char(&out, conversion);
                break;
        }
    }

    if (capacity && buffer) {
        usize end = out.length < capacity ? out.length : capacity - 1;
        buffer[end] = '\0';
    }
    return (int)out.length;
}

int ksnprintf(char *buffer, usize capacity, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = kvsnprintf(buffer, capacity, format, args);
    va_end(args);
    return result;
}
