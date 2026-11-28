#include <stdio.h>
#include <string.h>
#include "cvm.h"
#include "types.h"
#include "fixtures/aex3-functions.h"

static char captured[4096];
static usize captured_len;
static const char *test_input = "";
static int getchar_calls;

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

char serial_getc(void) { getchar_calls++; return '\n'; }
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
    if (image_size <= 0 || image[3] != '6') {
        fprintf(stderr, "compiler/VM test FAILED: compile error: %s\n", c_compile_error());
        return 1;
    }
    int result = c_execute(image);
    if (result != 42) {
        fprintf(stderr, "compiler/VM test FAILED: expected exit 42, got %d; output=%s\n", result, captured);
        return 1;
    }
    if (!strstr(captured, "answer=42\n")) {
        fail("printf output did not match the expected value");
        return 1;
    }

    /* String and character helpers operate on bounded Aiden character arrays. */
    captured_len = 0;
    captured[0] = 0;
    const char *strings_program =
        "#include <string.h>\n"
        "#include <ctype.h>\n"
        "int main(void) {\n"
        "  char text[32]; char tail[8]; char pad[8];\n"
        "  strcpy(text, \"DenOS\"); strcat(text, \" C\");\n"
        "  strcpy(tail, \"OS\"); strncpy(pad, \"A\", 4);\n"
        "  if (strlen(text) != 7) return 1;\n"
        "  if (strcmp(text, \"DenOS C\") != 0) return 2;\n"
        "  if (strncmp(strstr(text, tail), \"OS C\", 4) != 0) return 3;\n"
        "  if (*strchr(text, 'C') != 'C' || *strrchr(text, 'S') != 'S') return 4;\n"
        "  if (pad[0] != 'A' || pad[1] != 0 || pad[3] != 0) return 5;\n"
        "  if (atoi(\"-123\") != -123 || abs(-9) != 9 || labs(-11) != 11) return 6;\n"
        "  if (!isdigit('7') || !isalpha('Q') || !isspace(' ') || toupper('a') != 'A' || tolower('Z') != 'z') return 7;\n"
        "  if (!isalnum('8') || !isupper('A') || !islower('z') || !isxdigit('F')) return 8;\n"
        "  if (!iscntrl('\\n') || !isprint(' ') || !isgraph('!') || !ispunct('!')) return 9;\n"
        "  puts(text); printf(\" %s %u %x %X %o\\n\", text, 42, 255, 255, 8);\n"
        "  return 0;\n"
        "}\n";
    image_size = c_compile(strings_program, image, sizeof(image));
    if (image_size <= 0) {
        fprintf(stderr, "string test compile error: %s\n", c_compile_error());
        return 1;
    }
    result = c_execute(image);
    if (result != 0 || !strstr(captured, "DenOS C") || !strstr(captured, "42 ff FF 10")) {
        fprintf(stderr, "string test result=%d output=%s\n", result, captured);
        fail("string, character-class, or formatted-output built-ins failed");
        return 1;
    }

    /* Expanded C library coverage: bounded strings, character-set scans, and PRNG. */
    captured_len = 0;
    captured[0] = 0;
    const char *extended_lib_program =
        "#include <string.h>\n#include <stdlib.h>\n"
        "int main(void) { char source[32]; char output[32]; char *found; long first;\n"
        "  strcpy(source, \"aabccxyz\");\n"
        "  if (strnlen(source, 2) != 2 || strnlen(source, 32) != 8) return 1;\n"
        "  if (strspn(source, \"ab\") != 3 || strcspn(source, \"xy\") != 5) return 2;\n"
        "  found = strpbrk(source, \"yz\"); if (!found || *found != 'y') return 3;\n"
        "  strcpy(output, \"Den\"); strncat(output, \"OS kernel\", 2);\n"
        "  if (strcmp(output, \"DenOS\") != 0) return 4;\n"
        "  srand(1234); first = rand(); srand(1234);\n"
        "  if (first < 0 || rand() != first) return 5;\n"
        "  puts(output); return 0; }\n";
    image_size = c_compile(extended_lib_program, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 0 || !strstr(captured, "DenOS\n")) {
        fprintf(stderr, "extended library compile=%d error=%s output=%s\n",
                image_size, c_compile_error(), captured);
        fail("new bounded string, span/search, or random built-ins failed");
        return 1;
    }

    /* The new compiler limits accept >768 instructions and >8 KiB source files. */
    char larger_source[16000];
    usize larger_length = 0;
    const char *prefix = "int main(void) { int count = 0;\n";
    while (prefix[larger_length]) { larger_source[larger_length] = prefix[larger_length]; larger_length++; }
    for (int i = 0; i < 260; ++i) {
        const char *statement = "count += 1;\n";
        for (usize j = 0; statement[j]; ++j) larger_source[larger_length++] = statement[j];
    }
    const char *suffix = "return count == 260 ? 42 : 1; }\n";
    for (usize j = 0; suffix[j]; ++j) larger_source[larger_length++] = suffix[j];
    larger_source[larger_length] = 0;
    image_size = c_compile(larger_source, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "large program compile=%d error=%s\n", image_size, c_compile_error());
        fail("compiler did not accept a program larger than the old instruction limit");
        return 1;
    }

    /* A function frame can now use more than the previous 4096-cell limit. */
    const char *large_frame_program =
        "int main(void) { int first[3000]; int second[2000]; "
        "first[0] = 15; second[0] = 27; return first[0] + second[0]; }";
    image_size = c_compile(large_frame_program, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "large frame compile=%d error=%s\n", image_size, c_compile_error());
        fail("compiler did not accept a function frame larger than 4096 cells");
        return 1;
    }

    char long_source[12000];
    memset(long_source, ' ', 9000);
    strcpy(long_source + 9000, "int main(void) { return 42; }");
    image_size = c_compile(long_source, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "long source compile=%d error=%s\n", image_size, c_compile_error());
        fail("compiler did not accept source larger than 8 KiB");
        return 1;
    }

    /* AEX1 and AEX2 executables from earlier releases remain valid. */
    const char legacy_image[] = "AEX1" "0002" "0000"
                                 "012A000000" "1B00000000";
    const char prior_image[] = "AEX2" "0002" "0000"
                                "012A000000" "1B00000000";
    const char legacy_memory_image[] = "AEX1" "0004" "0000"
                                       "012A000000" "0500000000"
                                       "0400000000" "1B00000000";
    const char prior_memory_image[] = "AEX2" "0004" "0000"
                                      "012A000000" "0500000000"
                                      "0400000000" "1B00000000";
    const char aex3_image[] = "AEX3" "0002" "0000"
                              "012A000000" "1B00000000";
    const char aex5_image[] = "AEX5" "0002" "0000"
                              "012A000000" "1B00000000";
    if (c_execute(legacy_image) != 42 || c_execute(prior_image) != 42 ||
        c_execute(aex3_image) != 42 || c_execute(aex5_image) != 42 || c_execute(legacy_memory_image) != 42 ||
        c_execute(prior_memory_image) != 42) {
        fail("updated VM did not preserve AEX1/AEX2 executable compatibility, including global VM memory");
        return 1;
    }
    if (c_execute(aex3_functions_image) != 42) {
        fail("AEX3 compatibility failed for user functions and mutual recursion");
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
    const char *too_large = "int main(void) { return 9223372036854775808; }";
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

    captured_len = 0;
    captured[0] = 0;
    const char *scanf_auto_base =
        "int main(void) { long value; int converted = scanf(\"%i\", &value); "
        "printf(\"%d:%d\\n\", converted, value); return 0; }";
    test_input = "0x2A";
    image_size = c_compile(scanf_auto_base, image, sizeof(image));
    int auto_result = image_size > 0 ? c_execute(image) : -999;
    if (image_size <= 0 || auto_result != 0 || !strstr(captured, "1:42\n")) {
        fprintf(stderr, "scanf %%i hex size=%d result=%d compile=%s output=[%s]\n", image_size, auto_result, c_compile_error(), captured);
        fail("scanf %i did not recognize hexadecimal input");
        return 1;
    }
    captured_len = 0;
    captured[0] = 0;
    test_input = "052";
    image_size = c_compile(scanf_auto_base, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 0 || !strstr(captured, "1:42\n")) {
        fail("scanf %i did not recognize octal input");
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
    /* AEX3 carries the expanded instruction set and 64-bit constants. */
    captured_len = 0;
    captured[0] = 0;
    const char *wide_and_bits =
        "int main(void) { long value = 0x100000000ULL; "
        "value = (value >> 4) | 3; "
        "if ((value & 0x10000000) != 0 && (value ^ 3) == 0x10000000) "
        "return 42; return 1; }";
    image_size = c_compile(wide_and_bits, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "wide/bitwise test compile size=%d error=%s\n", image_size, c_compile_error());
        fail("64-bit constants, hex suffixes, shifts or bitwise operators failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *ternary_compound =
        "int main(void) { int value = 5; value += 3; value *= 2; "
        "value ^= 3; return value == 19 ? 42 : 1; }";
    image_size = c_compile(ternary_compound, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "ternary/compound test compile size=%d error=%s\n", image_size, c_compile_error());
        fail("ternary expression or compound assignment failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *scopes =
        "int main(void) { int value = 7; { int value = 99; value++; "
        "if (value != 100) return 1; } return value == 7 ? 42 : 1; }";
    image_size = c_compile(scopes, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "scope test compile size=%d error=%s\n", image_size, c_compile_error());
        fail("nested block scope or variable shadowing failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *loops =
        "int main(void) { int i = 0; int sum = 0; "
        "for (int n = 0; n < 10; n++) { if (n % 2 == 0) continue; sum += n; } "
        "do { i++; if (i == 3) continue; if (i == 6) break; sum += 1; } while (i < 9); "
        "return (sum == 29) ? 42 : 1; }";
    image_size = c_compile(loops, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "loop test compile size=%d error=%s\n", image_size, c_compile_error());
        fail("for/do-while, break or continue failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *arrays =
        "int main(void) { int values[4] = { 1, 2, 3, 4 }; "
        "int old = values[1]++; ++values[2]; "
        "return (old == 2 && values[1] == 3 && values[2] == 4) ? 42 : 1; }";
    image_size = c_compile(arrays, image, sizeof(image));
    int array_result = image_size > 0 ? c_execute(image) : -999;
    if (image_size <= 0 || array_result != 42) {
        fprintf(stderr, "array test compile size=%d result=%d error=%s\n", image_size, array_result, c_compile_error());
        fail("array initializer or pre/post increment failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    captured_len = 0;
    captured[0] = 0;
    const char *array_compound =
        "int main(void) { int data[3] = { 3, 6, 9 }; "
        "data[1] *= 4; data[2] -= 2; int old = data[1]--; "
        "return (old == 24 && data[1] == 23 && data[2] == 7) ? 42 : 1; }";
    image_size = c_compile(array_compound, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "array compound test size=%d error=%s\n", image_size, c_compile_error());
        fail("compound array updates failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *numeric_bases =
        "int main(void) { long long value = 0b1010 + 012 + 0x10; "
        "return value == 36 ? 42 : 1; }";
    image_size = c_compile(numeric_bases, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "numeric base test size=%d error=%s\n", image_size, c_compile_error());
        fail("binary, octal, hexadecimal or long long type parsing failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    getchar_calls = 0;
    const char *short_circuit =
        "int main(void) { int value = 0; "
        "if (0 && getchar()) value = 1; "
        "if (1 || getchar()) value += 2; "
        "return value == 2 ? 42 : 1; }";
    image_size = c_compile(short_circuit, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42 || getchar_calls != 0) {
        fprintf(stderr, "short circuit test size=%d calls=%d error=%s\n", image_size, getchar_calls, c_compile_error());
        fail("logical operators did not short-circuit");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *escape_test =
        "int main(void) { printf(\"%c:%d\\n\", '\\x41', '\\n'); return 42; }";
    image_size = c_compile(escape_test, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42 || !strstr(captured, "A:10\n")) {
        fprintf(stderr, "escape test size=%d output=%s error=%s\n", image_size, captured, c_compile_error());
        fail("hex and standard character escapes failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    captured_len = 0;
    captured[0] = 0;
    const char *functions_basic =
        "int add(int left, int right);\n"
        "int add(int a, int b) { int total = a + b; return total; }\n"
        "int main(void) { return add(19, 23); }";
    image_size = c_compile(functions_basic, image, sizeof(image));
    if (image_size <= 0 || image[3] != '6' || c_execute(image) != 42) {
        fprintf(stderr, "basic function test size=%d error=%s\n", image_size, c_compile_error());
        fail("prototype, parameters, local frame or function return failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *functions_recursive =
        "long factorial(long n) { if (n <= 1) return 1; return n * factorial(n - 1); }\n"
        "int fib(int n);\n"
        "int fib(int n) { int left; int right; if (n < 2) return n; "
        "left = fib(n - 1); right = fib(n - 2); return left + right; }\n"
        "int main(void) { return factorial(5) == 120 && fib(8) == 21 ? 42 : 1; }";
    image_size = c_compile(functions_recursive, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "recursion test size=%d error=%s\n", image_size, c_compile_error());
        fail("recursive function calls or independent stack frames failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *functions_mutual =
        "int is_odd(int n); int is_even(int n);\n"
        "int is_even(int n) { if (n == 0) return 1; return is_odd(n - 1); }\n"
        "int is_odd(int n) { if (n == 0) return 0; return is_even(n - 1); }\n"
        "int main(void) { return is_even(24) && is_odd(11) ? 42 : 1; }";
    image_size = c_compile(functions_mutual, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "mutual recursion test size=%d error=%s\n", image_size, c_compile_error());
        fail("mutual recursion and forward prototype patching failed");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *function_io =
        "int read_number(void) { int value = 0; scanf(\"%d\", &value); return value; }\n"
        "int main(void) { return read_number() == 42 ? 42 : 1; }";
    test_input = "42";
    image_size = c_compile(function_io, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "function I/O test size=%d error=%s\n", image_size, c_compile_error());
        fail("address-taking and scanf inside a function frame failed");
        return 1;
    }
    test_input = "";

    captured_len = 0;
    captured[0] = 0;
    const char *void_functions =
        "void announce(void) { puts(\"called a void function\"); return; } "
        "int main(void) { announce(); return 42; }";
    image_size = c_compile(void_functions, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42 ||
        !strstr(captured, "called a void function\n")) {
        fail("void functions or return-without-value failed");
        return 1;
    }

    const char *missing_prototype =
        "int main(void) { return later(1); } int later(int x) { return x; }";
    if (c_compile(missing_prototype, image, sizeof(image)) >= 0 ||
        !strstr(c_compile_error(), "prototype")) {
        fail("call to a later function without a prototype was not rejected clearly");
        return 1;
    }

    const char *bad_argument_count =
        "int inc(int x) { return x + 1; } int main(void) { return inc(); }";
    if (c_compile(bad_argument_count, image, sizeof(image)) >= 0 ||
        !strstr(c_compile_error(), "number of arguments")) {
        fail("wrong user-function argument count was not rejected");
        return 1;
    }

    const char *parameter_redeclaration =
        "int bad(int value) { int value = 5; return value; } "
        "int main(void) { return bad(1); }";
    if (c_compile(parameter_redeclaration, image, sizeof(image)) >= 0 ||
        !strstr(c_compile_error(), "duplicate variable")) {
        fail("function parameter could be redeclared in the outer function block");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *too_deep =
        "int descend(int n) { if (n == 0) return 0; return descend(n - 1) + 1; } "
        "int main(void) { return descend(200); }";
    image_size = c_compile(too_deep, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != -1 ||
        !strstr(captured, "function call stack limit")) {
        fail("VM did not stop excessive recursion safely");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *bad_shift = "int main(void) { return 1 << 64; }";
    image_size = c_compile(bad_shift, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != -1 || !strstr(captured, "invalid shift count")) {
        fail("VM did not reject an invalid shift count");
        return 1;
    }

    const char *line_error = "int main(void) {\n  return missing;\n}";
    if (c_compile(line_error, image, sizeof(image)) >= 0 ||
        !strstr(c_compile_error(), "line 2, column")) {
        fail("compiler did not report useful line/column diagnostics");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *bad_break = "int main(void) { break; return 0; }";
    if (c_compile(bad_break, image, sizeof(image)) >= 0 ||
        !strstr(c_compile_error(), "outside a loop")) {
        fail("break outside a loop was not rejected");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *single_element_array =
        "int main(void) { int only[1] = { 41 }; return only[0] + 1; }";
    image_size = c_compile(single_element_array, image, sizeof(image));
    if (image_size <= 0 || image[3] != '6' || c_execute(image) != 42) {
        fail("one-element arrays were not distinguished from scalar variables");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *array_overrun =
        "int main(void) { int values[2] = { 5, 6 }; return values[2]; }";
    image_size = c_compile(array_overrun, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != -1 ||
        !strstr(captured, "array index out of bounds")) {
        fail("out-of-range array reads were not trapped by the VM");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *array_negative =
        "int main(void) { int values[2] = { 5, 6 }; values[-1] = 9; return 0; }";
    image_size = c_compile(array_negative, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != -1 ||
        !strstr(captured, "array index out of bounds")) {
        fail("negative array writes were not trapped by the VM");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *divide_zero = "int main(void) { return 9 / 0; }";
    image_size = c_compile(divide_zero, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != -1 ||
        !strstr(captured, "division by zero")) {
        fail("division by zero did not stop the program");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *modulo_zero = "int main(void) { return 9 % 0; }";
    image_size = c_compile(modulo_zero, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != -1 ||
        !strstr(captured, "modulo by zero")) {
        fail("modulo by zero did not stop the program");
        return 1;
    }

    captured_len = 0;
    captured[0] = 0;
    const char *modulo_overflow =
        "int main(void) { long long minimum = -9223372036854775807LL - 1; "
        "return minimum % -1; }";
    image_size = c_compile(modulo_overflow, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != -1 ||
        !strstr(captured, "modulo overflow")) {
        fail("signed modulo overflow did not stop the program");
        return 1;
    }

    const char *truncated_image = "AEX1" "0002" "0000" "012A000000";
    const char *trailing_image = "AEX1" "0002" "0000"
                                 "012A000000" "1B00000000" "00";
    const char *oversized_global_image = "AEX6" "0002" "0008" "0101" "0000";
    if (c_execute(truncated_image) != -1 || c_execute(trailing_image) != -1 ||
        c_execute(oversized_global_image) != -1) {
        fail("VM accepted an image whose data length disagrees with its header");
        return 1;
    }

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

    /* AEX5 legacy features plus AEX6 global storage adds checked pointer-cell load/store, array decay, switch and enum constants. */
    captured_len = 0;
    captured[0] = 0;
    const char *pointer_test =
        "int bump(int *p);\n"
        "int bump(int *p) { *p += 3; return p[0]; }\n"
        "int main(void) { int a[3] = {2, 4, 6}; int *p = a; p++; "
        "if (bump(p) != 7) return 1; if (a[1] != 7) return 2; "
        "int n = 5; int *q = &n; *q = 11; "
        "return (n == 11 && sizeof(a) == 24 && sizeof(int *) == 8) ? 0 : 3; }";
    image_size = c_compile(pointer_test, image, sizeof(image));
    if (image_size <= 0 || image[3] != '6' || c_execute(image) != 0) {
        fprintf(stderr, "pointer/sizeof test compile size=%d error=%s\n",
                image_size, c_compile_error());
        fail("pointer parameters, pointer arithmetic, dereference, array decay, or sizeof failed");
        return 1;
    }

    const char *switch_test =
        "enum Color { RED = 2, GREEN, BLUE = 8 };\n"
        "int main(void) { int value = GREEN; int result = 0; "
        "switch (value) { case RED: result = 5; break; "
        "case GREEN: result = 10; break; default: result = 20; } "
        "switch (BLUE) { case RED: result = 1; break; case BLUE: result += 2; break; } "
        "return result == 12 ? 0 : 4; }";
    image_size = c_compile(switch_test, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 0) {
        fprintf(stderr, "switch/enum test compile size=%d error=%s\n",
                image_size, c_compile_error());
        fail("switch dispatch, fall-through control, break, or enum constants failed");
        return 1;
    }

    /* Aiden's small preprocessor supports object macros and basic conditionals. */
    const char *preprocessor_test =
        "#include <stdio.h>\n"
        "#define BASE 6\n"
        "#define ANSWER (BASE * 7)\n"
        "#define ENABLED 1\n"
        "#if 0\n"
        "this is deliberately invalid C and must be skipped\n"
        "#else\n"
        "#ifdef ENABLED\n"
        "typedef unsigned long count_t;\n"
        "count_t global_count = ANSWER;\n"
        "int main(void) { count_t local = global_count; "
        "puts(\"BASE is not expanded inside strings\"); "
        "return local == 42 ? 0 : 1; }\n"
        "#endif\n"
        "#endif\n";
    captured_len = 0; captured[0] = 0;
    image_size = c_compile(preprocessor_test, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 0 ||
        !strstr(captured, "BASE is not expanded inside strings")) {
        fprintf(stderr, "preprocessor test size=%d error=%s output=%s\n",
                image_size, c_compile_error(), captured);
        fail("object macros, conditional compilation, or scalar typedefs failed");
        return 1;
    }

    const char *recursive_macro_test =
        "#define LOOP LOOP\nint main(void) { return LOOP; }";
    if (c_compile(recursive_macro_test, image, sizeof(image)) >= 0 ||
        !strstr(c_compile_error(), "recursive macro")) {
        fail("recursive macro expansion was not diagnosed");
        return 1;
    }

    const char *unsupported_function_macro_test =
        "#define TWICE(x) ((x) * 2)\nint main(void) { return 0; }";
    if (c_compile(unsupported_function_macro_test, image, sizeof(image)) >= 0 ||
        !strstr(c_compile_error(), "function-like macros")) {
        fail("unsupported function-like macros were not diagnosed");
        return 1;
    }

    /* AEX6 adds persistent globals and pointers to global storage. */
    const char *global_storage_test =
        "int counter = 4; int values[3] = {3, 4, 5}; int *saved; "
        "int bump(void) { counter += 2; return counter; } "
        "int read_value(void) { return values[1]; } "
        "int main(void) { int counter = 10; "
        "if (bump() != 6 || read_value() != 4) return 1; "
        "counter += values[2]; saved = values; *(saved + 1) = 9; "
        "return (counter == 15 && values[1] == 9 && bump() == 8) ? 0 : 2; }";
    image_size = c_compile(global_storage_test, image, sizeof(image));
    if (image_size <= 0 || image[3] != '6' || c_execute(image) != 0) {
        fprintf(stderr, "global storage test compile size=%d error=%s output=%s\n",
                image_size, c_compile_error(), captured);
        fail("global scalar/array state, global pointer addressing, or local shadowing failed");
        return 1;
    }

    const char *global_bounds_test =
        "int data[2] = {1, 2}; int main(void) { return data[2]; }";
    captured_len = 0; captured[0] = 0;
    image_size = c_compile(global_bounds_test, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != -1 ||
        !strstr(captured, "array index out of bounds")) {
        fail("global array bounds were not enforced");
        return 1;
    }

    const char *pointer_bounds_test =
        "int main(void) { int x = 1; int *p = &x; return *(p + 5000); }";
    image_size = c_compile(pointer_bounds_test, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) == 0) {
        fail("invalid pointer dereference was not trapped");
        return 1;
    }

    /* An oversized identifier must be rejected without overflowing the
     * compiler's fixed-size local/global name buffers during error recovery. */
    char oversized_identifier_source[256];
    const char *oversized_local_prefix = "int main(void) { int ";
    usize oversized_used = 0;
    while (oversized_local_prefix[oversized_used]) {
        oversized_identifier_source[oversized_used] = oversized_local_prefix[oversized_used];
        ++oversized_used;
    }
    for (int i = 0; i < 80; ++i) oversized_identifier_source[oversized_used++] = 'x';
    const char *oversized_local_suffix = " = 1; return 0; }";
    for (usize i = 0; oversized_local_suffix[i]; ++i)
        oversized_identifier_source[oversized_used++] = oversized_local_suffix[i];
    oversized_identifier_source[oversized_used] = 0;
    if (c_compile(oversized_identifier_source, image, sizeof(image)) >= 0 ||
        !strstr(c_compile_error(), "identifier is too long")) {
        fail("overlong local identifiers were not safely rejected");
        return 1;
    }
    oversized_used = 0;
    for (int i = 0; i < 80; ++i) oversized_identifier_source[oversized_used++] = 'g';
    const char *oversized_global_suffix = "(void); int main(void) { return 0; }";
    for (usize i = 0; oversized_global_suffix[i]; ++i)
        oversized_identifier_source[oversized_used++] = oversized_global_suffix[i];
    oversized_identifier_source[oversized_used] = 0;
    if (c_compile(oversized_identifier_source, image, sizeof(image)) >= 0 ||
        !strstr(c_compile_error(), "identifier is too long")) {
        fail("overlong global identifiers were not safely rejected");
        return 1;
    }

    /* A single long source line used to allocate two 32 KiB arrays on the
     * small kernel stack. The preprocessor scratch must stay off-stack. */
    char long_line_source[30000];
    usize long_line_used = 0;
    long_line_source[long_line_used++] = '/';
    long_line_source[long_line_used++] = '*';
    for (int i = 0; i < 27000; ++i) long_line_source[long_line_used++] = 'x';
    const char *long_line_suffix = "*/ int main(void) { return 42; }";
    for (usize i = 0; long_line_suffix[i]; ++i)
        long_line_source[long_line_used++] = long_line_suffix[i];
    long_line_source[long_line_used] = 0;
    image_size = c_compile(long_line_source, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "long-line source compile=%d error=%s\n", image_size, c_compile_error());
        fail("preprocessor did not safely handle a source line larger than the kernel stack");
        return 1;
    }

    /* The documented 32 KiB source limit includes all source bytes while the
     * work buffers still need one extra terminator byte. */
    char exact_limit_source[32769];
    const char *exact_limit_suffix = "*/ int main(void) { return 42; }";
    usize exact_limit_suffix_length = strlen(exact_limit_suffix);
    exact_limit_source[0] = '/'; exact_limit_source[1] = '*';
    for (usize i = 2; i < 32768u - exact_limit_suffix_length; ++i) exact_limit_source[i] = 'x';
    memcpy(exact_limit_source + 32768u - exact_limit_suffix_length,
           exact_limit_suffix, exact_limit_suffix_length);
    exact_limit_source[32768] = 0;
    image_size = c_compile(exact_limit_source, image, sizeof(image));
    if (image_size <= 0 || c_execute(image) != 42) {
        fprintf(stderr, "exact-limit source compile=%d error=%s\n", image_size, c_compile_error());
        fail("compiler rejected or mishandled the exact 32768-byte source limit");
        return 1;
    }

    puts("Compiler/VM tests passed: legacy AEX compatibility, expanded AEX6 limits, macros/conditionals/typedefs, global scalars/arrays/pointers, switch/enums, sizeof, functions/recursion, string/ctype/search/PRNG built-ins, formatted I/O, operators/scopes/arrays, diagnostics, memory and arithmetic safety, malformed images, and instruction budget.");
    return 0;
}
