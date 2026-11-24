#include "calculator.h"

#define CALC_I64_MAX 9223372036854775807LL
#define CALC_I64_MIN (-9223372036854775807LL - 1LL)

typedef struct calculator_parser {
    const char *cursor;
    const char *error;
} calculator_parser_t;

static void skip_space(calculator_parser_t *parser) {
    while (*parser->cursor == ' ' || *parser->cursor == '\t') parser->cursor++;
}

static int parse_expression(calculator_parser_t *parser, i64 *value);

static int parse_number(calculator_parser_t *parser, i64 *value) {
    i64 result = 0;
    int digits = 0;
    skip_space(parser);
    while (*parser->cursor >= '0' && *parser->cursor <= '9') {
        int digit = *parser->cursor++ - '0';
        if (result > (CALC_I64_MAX - digit) / 10) {
            parser->error = "number is outside the signed 64-bit range";
            return -1;
        }
        result = result * 10 + digit;
        digits = 1;
    }
    if (!digits) {
        parser->error = "expected a number or parenthesized expression";
        return -1;
    }
    *value = result;
    return 0;
}

static int parse_factor(calculator_parser_t *parser, i64 *value) {
    skip_space(parser);
    if (*parser->cursor == '+') {
        parser->cursor++;
        return parse_factor(parser, value);
    }
    if (*parser->cursor == '-') {
        parser->cursor++;
        if (parse_factor(parser, value) < 0) return -1;
        *value = -*value;
        return 0;
    }
    if (*parser->cursor == '(') {
        parser->cursor++;
        if (parse_expression(parser, value) < 0) return -1;
        skip_space(parser);
        if (*parser->cursor != ')') {
            parser->error = "missing closing parenthesis";
            return -1;
        }
        parser->cursor++;
        return 0;
    }
    return parse_number(parser, value);
}

static int parse_term(calculator_parser_t *parser, i64 *value) {
    if (parse_factor(parser, value) < 0) return -1;
    for (;;) {
        char operation;
        i64 right, result;
        skip_space(parser);
        operation = *parser->cursor;
        if (operation != '*' && operation != '/' && operation != '%') return 0;
        parser->cursor++;
        if (parse_factor(parser, &right) < 0) return -1;
        if ((operation == '/' || operation == '%') && right == 0) {
            parser->error = "division by zero";
            return -1;
        }
        if (operation == '*') {
            if (__builtin_mul_overflow(*value, right, &result)) {
                parser->error = "multiplication overflow";
                return -1;
            }
        } else if (*value == CALC_I64_MIN && right == -1) {
            parser->error = "signed arithmetic overflow";
            return -1;
        } else if (operation == '/') result = *value / right;
        else result = *value % right;
        *value = result;
    }
}

static int parse_expression(calculator_parser_t *parser, i64 *value) {
    if (parse_term(parser, value) < 0) return -1;
    for (;;) {
        char operation;
        i64 right, result;
        skip_space(parser);
        operation = *parser->cursor;
        if (operation != '+' && operation != '-') return 0;
        parser->cursor++;
        if (parse_term(parser, &right) < 0) return -1;
        if (operation == '+') {
            if (__builtin_add_overflow(*value, right, &result)) {
                parser->error = "addition overflow";
                return -1;
            }
        } else if (__builtin_sub_overflow(*value, right, &result)) {
            parser->error = "subtraction overflow";
            return -1;
        }
        *value = result;
    }
}

int calculator_evaluate(const char *expression, i64 *result, const char **error) {
    calculator_parser_t parser;
    i64 value = 0;
    if (error) *error = "invalid calculator arguments";
    if (!expression || !result) return -1;
    parser.cursor = expression;
    parser.error = "invalid expression";
    skip_space(&parser);
    if (!*parser.cursor) {
        parser.error = "enter an expression";
        if (error) *error = parser.error;
        return -1;
    }
    if (parse_expression(&parser, &value) < 0) {
        if (error) *error = parser.error;
        return -1;
    }
    skip_space(&parser);
    if (*parser.cursor) {
        if (error) *error = "unexpected character in expression";
        return -1;
    }
    *result = value;
    if (error) *error = 0;
    return 0;
}
