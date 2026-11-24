#!/usr/bin/env python3
"""Static regression checks for NASM bootstrap symbol/BSS mistakes.

This is not a substitute for assembling boot.asm with NASM. It catches the
specific source regressions that previously broke the 9.5.1 boot build.
"""
from pathlib import Path
import re

source = Path('kernel/arch/x86_64/boot.asm').read_text(encoding='utf-8')

assert re.search(r'(?m)^section\s+\.bss\s*$', source), 'missing .bss section'
assert re.search(r'(?m)^alignb\s+4096\s*$', source), 'page-table BSS alignment must use alignb'
assert not re.search(r'(?m)^align\s+(?:16|4096)\s*$', source.split('section .text', 1)[0]), \
    'initialized alignment directive found in .bss'
assert re.search(r'(?m)^\s*lgdt\s+\[gdt64_pointer\]\s*$', source), 'lgdt target mismatch'
assert re.search(r'(?m)^gdt64_pointer:\s*$', source), 'GDT pointer label missing or locally scoped'
assert not re.search(r'(?m)^\.pointer:\s*$', source), 'GDT pointer must not be a local label'
print('Boot assembly source checks passed: BSS alignment and GDT pointer symbol are consistent.')
print('Note: run make all with NASM installed to perform actual assembly/link/ISO validation.')
