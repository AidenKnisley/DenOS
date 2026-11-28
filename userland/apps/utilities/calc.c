#include <stdio.h>
int main(void) {
    int left;
    int right;
    int result;
    char operation;
    puts("DenOS integer calculator");
    puts("Enter: number operator number (example: 12 * 3)");
    if (scanf("%d %c %d", &left, &operation, &right) != 3) {
        puts("Invalid input.");
        return 1;
    }
    if (operation == '+') result = left + right;
    else if (operation == '-') result = left - right;
    else if (operation == '*') result = left * right;
    else if (operation == '/') {
        if (right == 0) { puts("Division by zero."); return 1; }
        result = left / right;
    } else { puts("Unsupported operator."); return 1; }
    printf("Result: %d\n", result);
    return 0;
}
