#ifndef DENOS_PANIC_H
#define DENOS_PANIC_H

void kernel_panic_at(const char *reason, const char *file, int line)
    __attribute__((noreturn));

#define KPANIC(reason) kernel_panic_at((reason), __FILE__, __LINE__)
#define KASSERT(condition) do { \
    if (!(condition)) kernel_panic_at("kernel assertion failed: " #condition, \
                                      __FILE__, __LINE__); \
} while (0)

#endif
