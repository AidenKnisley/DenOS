BITS 64
section .text
global irq0_stub
extern timer_irq_handler
irq0_stub:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    mov r12, rsp
    and rsp, -16
    call timer_irq_handler
    mov rsp, r12
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    iretq
