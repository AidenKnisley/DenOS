#include <stdio.h>
#include <string.h>
#include "cvm.h"
#include "types.h"

static char captured[4096];
static usize captured_len;
static const char *test_input = "";

void serial_write(const char *text) {
    while (text && *text && captured_len + 1 < sizeof(captured)) {
        captured[captured_len++] = *text++;
    }
    captured[captured_len] = 0;
}

void serial_putc(char c) {
    if (captured_len + 1 < sizeof(captured)) {
        captured[captured_len++] = c;
        captured[captured_len] = 0;
    }
}

char serial_getc(void) { return '\n'; }
void serial_readline(char *buffer, usize capacity) {
    usize i = 0;
    if (!capacity) return;
    while (test_input[i] && i + 1 < capacity) {
        buffer[i] = test_input[i];
        i++;
    }
    buffer[i] = 0;
}
const char *fs_read(const char *name) { (void)name; return NULL; }

static void fail(const char *message) {
    fprintf(stderr, "compiler/VM test FAILED: %s\n", message);
    fprintf(stderr, "captured output: %s\n", captured);
    return;
}

int main(void) {
    char image[CVM_IMAGE_CAP];
    const char *program =
        "int main(void) {\n"
        "  int x = 20;\n"
        "  int y = 22;\n"
        "  printf(\"answer=%d\\n\", x + y);\n"
        "  return x + y;\n"
        "}\n";

    int image_size = c_compile(program, image, sizeof(image));
    if (image_size <= 0) {
        fprintf(stderr, "compiler/VM test FAILED: compile error: %s\n", c_compile_error());
        return 1;
    }
    int result = c_execute(image);
    if (result != 42) {
        fprintf(stderr, "compiler/VM test FAILED: expected exit 42, got %d\n", result);
        return 1;
    }
    if (!strstr(captured, "answer=42\n")) {
        fail("printf output did not match the expected value");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *wrap_test =
        "int main(void) { long x = 46341; x = x * x; x = x * x; "
        "x = x * 2; if (x < 0) return 1; return 0; }";
    image_size = c_compile(wrap_test, image, sizeof(image));
    int wrap_result = image_size > 0 ? c_execute(image) : -999;
    if (image_size <= 0 || wrap_result != 1) {
        fprintf(stderr, "wrap test compile size=%d result=%d error=%s\n",
                image_size, wrap_result, c_compile_error());
        fail("signed guest arithmetic did not wrap consistently");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *too_large = "int main(void) { return 2147483648; }";
    if (c_compile(too_large, image, sizeof(image)) >= 0) {
        fail("out-of-range integer literal was accepted");
        return 1;
    }


    captured_len = 0;
    captured[0] = 0;
    test_input = "12345";
    const char *scanf_ok =
        "int main(void) { long value; int converted = scanf(\"%d\", &value); "
        "printf(\"%d:%d\\n\", converted, value); return 0; }";
    image_size = c_compile(scanf_ok, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 0 || !strstr(captured, "1:12345\n")) {
        fail("scanf did not parse and store a normal signed integer");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    test_input = "9223372036854775808";
    image_size = c_compile(scanf_ok, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 0 || !strstr(captured, "0:0\n")) {
        fail("scanf did not reject signed 64-bit input overflow safely");
        return 1;
    }
    test_input = "";

    captured_len = 0;
    captured[0] = 0;
    const char *unterminated_comment =
        "int main(void) { return 0; } /* missing end";
    if (c_compile(unterminated_comment, image, sizeof(image)) >= 0) {
        fail("unterminated block comment was accepted");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *endless = "int main(void) { while (1) { } }";
    image_size = c_compile(endless, image, sizeof(image));
    if (image_size <= 0) {
        fprintf(stderr, "compiler/VM test FAILED: infinite-loop test did not compile: %s\n",
                c_compile_error());
        return 1;
    }
    if (c_execute(image) != -1 || !strstr(captured, "instruction limit reached")) {
        fail("instruction budget did not stop an endless loop");
        return 1;
    }

    puts("Compiler/VM tests passed: compile/execute, printf, scanf range checks, comments, integer range checks, and instruction budget.");
    return 0;
}
