#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

int main(void) {
    unsigned char bytes[32];
    int supported = cpu_has_rdrand();
    int result = cpu_random_bytes(bytes, sizeof(bytes));
    if (supported) CHECK(result == 0);
    else CHECK(result < 0);
    CHECK(cpu_random_bytes(NULL, 0) == 0);
    CHECK(cpu_random_bytes(NULL, 1) < 0);
    puts("CPU random-source tests passed: capability detection and fail-closed byte generation.");
    return 0;
}
