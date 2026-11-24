#include "exception.h"
#include "serial.h"
#include "cpu.h"

_Static_assert(__builtin_offsetof(exception_frame_t, vector) == 120u,
               "exception register frame layout mismatch");
_Static_assert(__builtin_offsetof(exception_frame_t, error_code) == 128u,
               "exception error-code offset mismatch");
_Static_assert(__builtin_offsetof(exception_frame_t, rip) == 136u,
               "exception CPU frame offset mismatch");
_Static_assert(sizeof(exception_frame_t) == 176u,
               "exception frame size mismatch");

extern char stack_bottom[];
extern char stack_top[];

static const char *const exception_names[32] = {
    "Divide Error", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "BOUND Range Exceeded", "Invalid Opcode",
    "Device Not Available", "Double Fault", "Reserved (vector 9)",
    "Invalid TSS", "Segment Not Present", "Stack-Segment Fault",
    "General Protection Fault", "Page Fault", "Reserved (vector 15)",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check",
    "SIMD Floating-Point Exception", "Virtualization Exception",
    "Control Protection Exception", "Reserved (vector 22)",
    "Reserved (vector 23)", "Reserved (vector 24)", "Reserved (vector 25)",
    "Reserved (vector 26)", "Reserved (vector 27)",
    "Hypervisor Injection Exception", "VMM Communication Exception",
    "Security Exception", "Reserved (vector 31)"
};

const char *exception_name(u64 vector) {
    return vector < 32u ? exception_names[vector] : "Unknown Exception";
}

int exception_has_error_code(u64 vector) {
    switch (vector) {
        case 8: case 10: case 11: case 12: case 13: case 14:
        case 17: case 21: case 29: case 30:
            return 1;
        default:
            return 0;
    }
}

static u16 current_ss(void) {
    u16 value;
    __asm__ volatile("mov %%ss, %0" : "=r"(value));
    return value;
}

u64 exception_saved_rsp(const exception_frame_t *frame) {
    if (!frame) return 0;
    if (frame->vector == 8u || (frame->cs & 3u) != 0u) return frame->rsp;
    return (u64)(usize)frame + __builtin_offsetof(exception_frame_t, rsp);
}

u64 exception_saved_ss(const exception_frame_t *frame) {
    if (!frame) return 0;
    if (frame->vector == 8u || (frame->cs & 3u) != 0u) return frame->ss;
    return (u64)current_ss();
}

void exception_decode_page_fault(u64 error,
                                 exception_page_fault_info_t *out) {
    if (!out) return;
    out->protection_violation = (u8)((error >> 0) & 1u);
    out->write = (u8)((error >> 1) & 1u);
    out->user = (u8)((error >> 2) & 1u);
    out->reserved_bit = (u8)((error >> 3) & 1u);
    out->instruction_fetch = (u8)((error >> 4) & 1u);
    out->protection_key = (u8)((error >> 5) & 1u);
    out->shadow_stack = (u8)((error >> 6) & 1u);
    out->sgx = (u8)((error >> 15) & 1u);
}

static void write_hex(const char *label, u64 value) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[16];
    int used = 0;
    int started = 0;
    serial_write(label);
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        u8 nibble = (u8)((value >> shift) & 0xFu);
        if (nibble || started || shift == 0) {
            buffer[used++] = digits[nibble];
            started = 1;
        }
    }
    while (used) serial_putc(buffer[--used]);
    serial_putc('\n');
}

static u64 read_cr2(void) {
    u64 value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void dump_stack_trace(u64 frame_pointer) {
    u64 low = (u64)(usize)stack_bottom;
    u64 high = (u64)(usize)stack_top;
    u64 previous = 0;
    unsigned depth = 0;

    serial_write("\nStack trace (return addresses; symbols require a map file):\n");
    while (depth < 8u && frame_pointer >= low &&
           frame_pointer <= high - 16u && (frame_pointer & 7u) == 0u &&
           frame_pointer > previous) {
        const volatile u64 *frame = (const volatile u64 *)(usize)frame_pointer;
        u64 next = frame[0];
        u64 return_address = frame[1];
        if (!return_address) break;
        serial_write("  #");
        serial_putc((char)('0' + depth));
        serial_write(" ");
        write_hex("", return_address);
        depth++;
        if (next <= frame_pointer || next > high - 16u) break;
        previous = frame_pointer;
        frame_pointer = next;
    }
    if (!depth) serial_write("  <frame chain unavailable>\n");
}

void exception_dispatch(const exception_frame_t *frame) __attribute__((noreturn));
void exception_dispatch(const exception_frame_t *frame) {
    exception_page_fault_info_t pf;
    u64 address = 0;

    cpu_interrupt_disable();
    serial_write("\033[0m\033[41;37m\n*** DENOS CPU EXCEPTION ***\033[0m\n");
    if (!frame) {
        serial_write("No exception frame available. CPU halted.\n");
        cpu_halt();
    }

    serial_write("Exception: ");
    serial_write(exception_name(frame->vector));
    serial_write(" (vector ");
    serial_putc((char)('0' + (frame->vector / 10u) % 10u));
    serial_putc((char)('0' + frame->vector % 10u));
    serial_write(")\n");
    write_hex("Error code: ", frame->error_code);
    write_hex("RIP:        ", frame->rip);
    write_hex("RSP:        ", exception_saved_rsp(frame));
    write_hex("RBP:        ", frame->rbp);
    write_hex("RFLAGS:     ", frame->rflags);
    write_hex("CS:         ", frame->cs);
    write_hex("SS:         ", exception_saved_ss(frame));
    write_hex("RAX:        ", frame->rax);
    write_hex("RBX:        ", frame->rbx);
    write_hex("RCX:        ", frame->rcx);
    write_hex("RDX:        ", frame->rdx);
    write_hex("RSI:        ", frame->rsi);
    write_hex("RDI:        ", frame->rdi);
    write_hex("R8:         ", frame->r8);
    write_hex("R9:         ", frame->r9);
    write_hex("R10:        ", frame->r10);
    write_hex("R11:        ", frame->r11);
    write_hex("R12:        ", frame->r12);
    write_hex("R13:        ", frame->r13);
    write_hex("R14:        ", frame->r14);
    write_hex("R15:        ", frame->r15);

    if (frame->vector == 14u) {
        address = read_cr2();
        write_hex("CR2 fault address: ", address);
        exception_decode_page_fault(frame->error_code, &pf);
        serial_write("Page fault: ");
        serial_write(pf.protection_violation ? "protection violation" : "not-present page");
        serial_write(pf.write ? ", write" : ", read");
        serial_write(pf.user ? ", user access" : ", supervisor access");
        if (pf.reserved_bit) serial_write(", reserved paging bit set");
        if (pf.instruction_fetch) serial_write(", instruction fetch");
        if (pf.protection_key) serial_write(", protection-key violation");
        if (pf.shadow_stack) serial_write(", shadow-stack access");
        if (pf.sgx) serial_write(", SGX-related");
        serial_putc('\n');
    }

    dump_stack_trace(frame->rbp);
    serial_write("\nThe kernel cannot safely resume. System halted.\n");
    cpu_halt();
}
