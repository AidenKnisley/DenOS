#ifndef DENOS_CALCULATOR_H
#define DENOS_CALCULATOR_H
#include "types.h"

/* Evaluate an integer expression with +, -, *, /, %, unary signs and
 * parentheses. Returns zero on success and a stable diagnostic on failure. */
int calculator_evaluate(const char *expression, i64 *result, const char **error);

#endif
