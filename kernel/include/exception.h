#ifndef DENOS_EXCEPTION_H
#define DENOS_EXCEPTION_H

#include "types.h"

/* Register layout produced by interrupts.asm. The CPU always saves
 * RIP/CS/RFLAGS, but saves RSP/SS only on privilege transitions or IST.
 * exception_saved_rsp/ss() resolve those optional fields. */
typedef struct exception_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 vector;
    u64 error_code;
    u64 rip, cs, rflags, rsp, ss;
} exception_frame_t;

typedef struct exception_page_fault_info {
    u8 protection_violation;
    u8 write;
    u8 user;
    u8 reserved_bit;
    u8 instruction_fetch;
    u8 protection_key;
    u8 shadow_stack;
    u8 sgx;
} exception_page_fault_info_t;

const char *exception_name(u64 vector);
int exception_has_error_code(u64 vector);
void exception_decode_page_fault(u64 error,
                                 exception_page_fault_info_t *out);
/* Resolve optional hardware-saved RSP/SS fields correctly. For same-CPL
 * exceptions, the CPU does not push RSP/SS; RSP is derived from the frame. */
u64 exception_saved_rsp(const exception_frame_t *frame);
u64 exception_saved_ss(const exception_frame_t *frame);
void exception_dispatch(const exception_frame_t *frame) __attribute__((noreturn));

#endif
