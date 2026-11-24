/* Aiden C function demo: parameters, return values, and recursion. */
long factorial(long value) {
    if (value <= 1) return 1;
    return value * factorial(value - 1);
}

int fibonacci(int value) {
    int previous;
    int current;

    if (value < 2) return value;
    previous = fibonacci(value - 1);
    current = fibonacci(value - 2);
    return previous + current;
}

int main(void) {
    puts("Aiden C function demo");
    printf("factorial(6) = %d\n", factorial(6));
    printf("fibonacci(10) = %d\n", fibonacci(10));
    return 0;
}
