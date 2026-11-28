#include "terminal_apps.h"
#include "serial.h"
#include "calculator.h"
#include "kformat.h"
#include "terminal_ui.h"

void terminal_calculator_run(const char *expression) {
    char input[256];
    const char *error = 0;
    i64 result = 0;
    if (!expression || !expression[0]) {
        serial_write("Expression: ");
        serial_readline(input, sizeof(input));
        expression = input;
    }
    if (!expression[0]) return;
    if (calculator_evaluate(expression, &result, &error) < 0) {
        serial_write(TERM_RED "calc: " TERM_RESET);
        serial_write(error ? error : "invalid expression");
        serial_write("\n");
        return;
    }
    serial_write("= ");
    {
        char output[40];
        (void)ksnprintf(output, sizeof(output), "%lld", (long long)result);
        serial_write(output);
    }
    serial_write("\n");
}

