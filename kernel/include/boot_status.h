#ifndef DENOS_BOOT_STATUS_H
#define DENOS_BOOT_STATUS_H

#include "types.h"

#define BOOT_STATUS_CAP 16
#define BOOT_LABEL_CAP 28

enum boot_result {
    BOOT_RESULT_OK = 0,
    BOOT_RESULT_WARN,
    BOOT_RESULT_FAIL
};

typedef struct boot_record {
    char label[BOOT_LABEL_CAP];
    u64 tick;
    u8 result;
} boot_record_t;

void boot_status_init(void);
int boot_status_add(const char *label, enum boot_result result);
int boot_status_count(void);
const boot_record_t *boot_status_get(int index);
const char *boot_result_name(enum boot_result result);

#endif
