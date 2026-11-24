BITS 64
section .text

; Timer IRQ. This is deliberately separate from exception handling.
global irq0_stub
extern timer_irq_handler
irq0_stub:
    ; An IRQ can interrupt code while any register is live. Preserve every
    ; general-purpose register, not just the SysV caller-saved subset.
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    and rsp, -16
    sub rsp, 512
    fxsave64 [rsp]
    cld
    call timer_irq_handler
    fxrstor64 [rsp]
    mov rsp, r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

extern exception_dispatch

%macro EXCEPTION_NO_ERROR 1
    global exception_stub_%1
exception_stub_%1:
    push qword 0                 ; Normalize the error-code slot.
    push qword %1
    jmp exception_common
%endmacro

%macro EXCEPTION_ERROR 1
    global exception_stub_%1
exception_stub_%1:
    ; The processor already pushed the 64-bit, zero-extended error code.
    push qword %1
    jmp exception_common
%endmacro

EXCEPTION_NO_ERROR 0
EXCEPTION_NO_ERROR 1
EXCEPTION_NO_ERROR 2
EXCEPTION_NO_ERROR 3
EXCEPTION_NO_ERROR 4
EXCEPTION_NO_ERROR 5
EXCEPTION_NO_ERROR 6
EXCEPTION_NO_ERROR 7
EXCEPTION_ERROR    8
EXCEPTION_NO_ERROR 9
EXCEPTION_ERROR    10
EXCEPTION_ERROR    11
EXCEPTION_ERROR    12
EXCEPTION_ERROR    13
EXCEPTION_ERROR    14
EXCEPTION_NO_ERROR 15
EXCEPTION_NO_ERROR 16
EXCEPTION_ERROR    17
EXCEPTION_NO_ERROR 18
EXCEPTION_NO_ERROR 19
EXCEPTION_NO_ERROR 20
EXCEPTION_ERROR    21
EXCEPTION_NO_ERROR 22
EXCEPTION_NO_ERROR 23
EXCEPTION_NO_ERROR 24
EXCEPTION_NO_ERROR 25
EXCEPTION_NO_ERROR 26
EXCEPTION_NO_ERROR 27
EXCEPTION_NO_ERROR 28
EXCEPTION_ERROR    29
EXCEPTION_ERROR    30
EXCEPTION_NO_ERROR 31

exception_common:
    ; Frame layout at the C entry is exception_frame_t in exception.h.
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov r12, rsp                 ; Preserve the frame pointer across alignment.
    mov rdi, rsp
    and rsp, -16
    cld
    call exception_dispatch
    mov rsp, r12

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                  ; Remove vector and normalized error code.
    iretq                        ; Exception handler is fatal; this is defensive.

section .rodata
align 8
global exception_stub_table
exception_stub_table:
%assign i 0
%rep 32
    dq exception_stub_%+i
%assign i i+1
%endrep
