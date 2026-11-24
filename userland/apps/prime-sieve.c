/* Bounded sieve example using fixed arrays and loops in Aiden C. */
int main(void) {
    int composite[101] = {0};
    int limit = 100;
    int count = 0;
    for (int p = 2; p * p <= limit; ++p) {
        if (!composite[p]) {
            for (int multiple = p * p; multiple <= limit; multiple += p)
                composite[multiple] = 1;
        }
    }
    puts("Primes through 100:\n");
    for (int value = 2; value <= limit; ++value) {
        if (!composite[value]) {
            printf("%d ", value);
            ++count;
        }
    }
    printf("\nFound %d primes.\n", count);
    return count == 25 ? 0 : 1;
}
