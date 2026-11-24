#include <stdio.h>
int main(void) {
    int number;
    puts("Guess a number from 1 to 5:");
    if (scanf("%d", &number) != 1) return 1;
    if (number == 3) puts("Correct!");
    else puts("Nope, it was 3.");
    return 0;
}
