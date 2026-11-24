#ifndef AIDEN_CVM_H
#define AIDEN_CVM_H
#include "types.h"
#define CVM_IMAGE_CAP 16000
int c_compile(const char *source, char *image, usize capacity);
const char *c_compile_error(void);
int c_execute(const char *image);
int program_run(const char *name);
#endif
