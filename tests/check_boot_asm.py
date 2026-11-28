#!/usr/bin/env python3
"""Static checks for the terminal-only Multiboot2 boot path.

This is not a substitute for assembling boot.asm with NASM. It catches
source-level regressions in the text-only GRUB configuration, BSS alignment,
and GDT pointer.
"""
from pathlib import Path
import re

source = Path('kernel/arch/x86_64/boot.asm').read_text(encoding='utf-8')
grub_cfg = Path('boot/grub.cfg').read_text(encoding='utf-8')
kernel = Path('kernel/kernel.c').read_text(encoding='utf-8')

assert 'terminal_output console' in grub_cfg, 'GRUB must use text console output'
assert 'gfxmode' not in grub_cfg and 'gfxpayload' not in grub_cfg, 'GRUB config must not request graphics mode'
assert not re.search(r'(?s)dw\s+5\s*dw\s+1\s*dd\s+20\s*dd\s+\d+\s*dd\s+\d+\s*dd\s+32', source), \
    'Multiboot2 framebuffer request must not be present'
assert 'gfx_configure_framebuffer' not in kernel and 'desktop_run' not in kernel, \
    'kernel boot path must not initialize or launch a GUI'
assert re.search(r'(?s)dw\s+0\s*dw\s+0\s*dd\s+8', source), 'aligned Multiboot2 end tag is missing'
assert re.search(r'(?m)^section\s+\.bss\s*$', source), 'missing .bss section'
assert re.search(r'(?m)^alignb\s+4096\s*$', source), 'page-table BSS alignment must use alignb'
assert not re.search(r'(?m)^align\s+(?:16|4096)\s*$', source.split('section .text', 1)[0]), \
    'initialized alignment directive found in .bss'
assert re.search(r'(?m)^\s*lgdt\s+\[gdt64_pointer\]\s*$', source), 'lgdt target mismatch'
assert re.search(r'(?m)^gdt64_pointer:\s*$', source), 'GDT pointer label missing or locally scoped'
assert not re.search(r'(?m)^\.pointer:\s*$', source), 'GDT pointer must not be a local label'
print('Boot assembly checks passed: text-only GRUB path, no framebuffer request, aligned end tag, BSS alignment, and GDT pointer.')
print('Note: run make build with NASM installed to perform actual assembly/link/ISO validation.')
