#ifndef DENOS_ELF64_H
#define DENOS_ELF64_H
#include "types.h"
typedef struct elf64_info { u16 type; u16 machine; u16 program_headers; u16 section_headers; u64 entry; u64 program_header_offset; u64 section_header_offset; } elf64_info_t;
/* Validates ELF64 structure and load-segment bounds; it does not execute files. */
int elf64_validate(const void *image,usize length,elf64_info_t *info);
#endif
