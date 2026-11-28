/* Euclid's algorithm and primality test; demonstrates functions and modulo. */
int gcd(int a, int b) {
    while (b != 0) {
        int remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

int is_prime(int value) {
    if (value < 2) return 0;
    for (int divisor = 2; divisor * divisor <= value; ++divisor)
        if (value % divisor == 0) return 0;
    return 1;
}

int main(void) {
    printf("gcd(84, 30) = %d\n", gcd(84, 30));
    puts("Primes below 40: ");
    for (int n = 2; n < 40; ++n)
        if (is_prime(n)) printf("%d ", n);
    puts("\n");
    return gcd(84, 30) == 6 && is_prime(37) ? 0 : 1;
}
