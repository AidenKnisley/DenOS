BITS 32
section .multiboot
align 8
mb2_header_start:
    dd 0xe85250d6
    dd 0
    dd mb2_header_end - mb2_header_start
    dd -(0xe85250d6 + 0 + (mb2_header_end - mb2_header_start))
    dw 0
    dw 0
    dd 8
mb2_header_end:

section .bss
align 16
stack_bottom: resb 16384
stack_top:
align 4096
pml4: resq 512
pdpt: resq 512
pd:   resq 512

section .text
global _start
extern kernel_main
_start:
    cli
    mov esp, stack_top
    ; Identity map the first 1 GiB using 2 MiB pages.
    mov eax, pd
    or eax, 0x3
    mov [pdpt], eax
    mov eax, pdpt
    or eax, 0x3
    mov [pml4], eax
    mov ecx, 0
.map_pd:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83
    mov [pd + ecx*8], eax
    inc ecx
    cmp ecx, 512
    jne .map_pd
    mov eax, pml4
    mov cr3, eax
    ; Enable PAE plus the architectural OS support required by SSE/SSE2.
    ; GCC may use XMM instructions in optimized kernel code (including the VM).
    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax

    ; Enable the x87 FPU and clear EM/TS so x87/SSE instructions do not fault.
    mov eax, cr0
    and eax, ~(1 << 2)       ; CR0.EM = 0
    and eax, ~(1 << 3)       ; CR0.TS = 0
    or eax, (1 << 1)         ; CR0.MP = 1
    mov cr0, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    lgdt [gdt64.pointer]
    jmp 0x08:long_mode

BITS 64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    mov rsp, stack_top
    and rsp, -16
    xor rbp, rbp
    fninit
    call kernel_main
.hang: hlt
    jmp .hang

align 8
gdt64:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
.pointer:
    dw $ - gdt64 - 1
    dq gdt64
