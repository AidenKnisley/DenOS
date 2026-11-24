#include "exception.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char stack_bottom[16384];
char stack_top[1];
void serial_write(const char *text) { (void)text; }
void serial_putc(char c) { (void)c; }
void cpu_interrupt_disable(void) {}
void cpu_halt(void) { abort(); }

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "exception_test:%d: %s\n", __LINE__, #x); return 1; \
} } while (0)

int main(void) {
    exception_page_fault_info_t info;
    exception_frame_t frame;

    CHECK(strcmp(exception_name(0), "Divide Error") == 0);
    CHECK(strcmp(exception_name(6), "Invalid Opcode") == 0);
    CHECK(strcmp(exception_name(8), "Double Fault") == 0);
    CHECK(strcmp(exception_name(13), "General Protection Fault") == 0);
    CHECK(strcmp(exception_name(14), "Page Fault") == 0);
    CHECK(strcmp(exception_name(31), "Reserved (vector 31)") == 0);
    CHECK(strcmp(exception_name(32), "Unknown Exception") == 0);

    CHECK(exception_has_error_code(8));
    CHECK(exception_has_error_code(10));
    CHECK(exception_has_error_code(13));
    CHECK(exception_has_error_code(14));
    CHECK(exception_has_error_code(21));
    CHECK(exception_has_error_code(29));
    CHECK(exception_has_error_code(30));
    CHECK(!exception_has_error_code(0));
    CHECK(!exception_has_error_code(7));
    CHECK(!exception_has_error_code(20));
    CHECK(!exception_has_error_code(31));

    memset(&info, 0xFF, sizeof(info));
    exception_decode_page_fault(0, &info);
    CHECK(!info.protection_violation && !info.write && !info.user &&
          !info.reserved_bit && !info.instruction_fetch &&
          !info.protection_key && !info.shadow_stack && !info.sgx);

    exception_decode_page_fault((1u << 0) | (1u << 1) | (1u << 2) |
                                (1u << 3) | (1u << 4) | (1u << 5) |
                                (1u << 6) | (1u << 15), &info);
    CHECK(info.protection_violation && info.write && info.user &&
          info.reserved_bit && info.instruction_fetch &&
          info.protection_key && info.shadow_stack && info.sgx);

    memset(&frame, 0, sizeof(frame));
    frame.vector = 14;
    frame.cs = 0x08; /* Same privilege: CPU did not save RSP/SS. */
    frame.rsp = 0xDEADBEEF;
    CHECK(exception_saved_rsp(&frame) ==
          (u64)(unsigned long)&frame + __builtin_offsetof(exception_frame_t, rsp));

    frame.vector = 8; /* Double fault uses IST1 and does save RSP/SS. */
    frame.rsp = 0x12345000;
    frame.ss = 0x10;
    CHECK(exception_saved_rsp(&frame) == 0x12345000);
    CHECK(exception_saved_ss(&frame) == 0x10);

    frame.vector = 14;
    frame.cs = 0x1B; /* Future ring-3 exception frame. */
    frame.rsp = 0x70000000;
    frame.ss = 0x23;
    CHECK(exception_saved_rsp(&frame) == 0x70000000);
    CHECK(exception_saved_ss(&frame) == 0x23);

    puts("exception tests: OK");
    return 0;
}
