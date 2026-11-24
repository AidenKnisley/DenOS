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
alignb 16
global stack_bottom
global stack_top
stack_bottom: resb 16384
stack_top:
 alignb 16
global double_fault_stack_bottom
global double_fault_stack_top
double_fault_stack_bottom: resb 16384
double_fault_stack_top:
 alignb 16
global interrupt_stack_bottom
global interrupt_stack_top
interrupt_stack_bottom: resb 16384
interrupt_stack_top:
 alignb 16
global tss64
tss64: resb 104
mb2_magic: resd 1
mb2_info:  resd 1
alignb 4096
pml4: resq 512
pdpt: resq 512
pd0:  resq 512
pd1:  resq 512
pd2:  resq 512
pd3:  resq 512

section .text
global _start
extern kernel_main
_start:
    cli
    ; Preserve the Multiboot2 boot magic (EAX) and information pointer (EBX).
    ; The 32-bit setup below reuses both registers before entering long mode.
    mov [mb2_magic], eax
    mov [mb2_info], ebx
    mov esp, stack_top
    ; Identity map the first 4 GiB using 2 MiB pages. This covers kernel
    ; memory, the legacy VGA text buffer, and PCI MMIO windows used by drivers.
    mov eax, pd0
    or eax, 0x3
    mov [pdpt + 0*8], eax
    mov eax, pd1
    or eax, 0x3
    mov [pdpt + 1*8], eax
    mov eax, pd2
    or eax, 0x3
    mov [pdpt + 2*8], eax
    mov eax, pd3
    or eax, 0x3
    mov [pdpt + 3*8], eax
    mov eax, pdpt
    or eax, 0x3
    mov [pml4], eax
    mov ecx, 0
.map_pd:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83
    mov edx, ecx
    shr edx, 9
    shl edx, 12
    mov ebp, pd0
    add edx, ebp
    mov edi, ecx
    and edi, 0x1ff
    shl edi, 3
    mov [edx + edi], eax
    inc ecx
    cmp ecx, 2048
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
    lgdt [gdt64_pointer]
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
    ; SysV AMD64 arguments: RDI = boot magic, RSI = physical info pointer.
    mov edi, [mb2_magic]
    mov esi, [mb2_info]
    call kernel_main
.hang: hlt
    jmp .hang

section .data
align 8
gdt64:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
    global gdt_tss_descriptor
gdt_tss_descriptor:
    dq 0
    dq 0
gdt64_pointer:
    dw $ - gdt64 - 1
    dq gdt64
