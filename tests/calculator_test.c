#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calculator.h"

static void check(const char *expression, i64 expected) {
    i64 result = 0;
    const char *error = 0;
    if (calculator_evaluate(expression, &result, &error) != 0 || result != expected) {
        fprintf(stderr, "calculator failed: %s (got %lld, error %s)\n", expression,
                (long long)result, error ? error : "none");
        exit(1);
    }
}
static void reject(const char *expression, const char *expected_error) {
    i64 result = 0;
    const char *error = 0;
    if (calculator_evaluate(expression, &result, &error) == 0 || !error ||
        strcmp(error, expected_error) != 0) {
        fprintf(stderr, "calculator accepted/reported wrong error for: %s\n", expression);
        exit(1);
    }
}
int main(void) {
    check("1 + 2 * 3", 7);
    check("(1 + 2) * 3", 9);
    check("-12 + 5", -7);
    check("90 / 3 / 2", 15);
    check("25 % 7", 4);
    check("2 * (3 + (4 * 5))", 46);
    check("  100  -  30 ", 70);
    reject("10 / 0", "division by zero");
    reject("(3 + 4", "missing closing parenthesis");
    reject("2 + nope", "expected a number or parenthesized expression");
    reject("9223372036854775807 + 1", "addition overflow");
    reject("3037000500 * 3037000500", "multiplication overflow");
    reject("", "enter an expression");
    puts("Calculator tests passed: precedence, parentheses, unary signs, integer arithmetic, whitespace, division by zero, malformed expressions, and overflow protection.");
    return 0;
}
