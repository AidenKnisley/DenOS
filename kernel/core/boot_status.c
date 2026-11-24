#include "boot_status.h"
#include "timer.h"
#include "kstring.h"

static boot_record_t records[BOOT_STATUS_CAP];
static int record_count;

void boot_status_init(void) {
    for (int i = 0; i < BOOT_STATUS_CAP; i++) {
        records[i].label[0] = '\0';
        records[i].tick = 0;
        records[i].result = BOOT_RESULT_OK;
    }
    record_count = 0;
}

int boot_status_add(const char *label, enum boot_result result) {
    if (!label || !label[0] || result < BOOT_RESULT_OK ||
        result > BOOT_RESULT_FAIL || record_count >= BOOT_STATUS_CAP ||
        kstrnlen(label, BOOT_LABEL_CAP) >= BOOT_LABEL_CAP)
        return -1;
    boot_record_t *record = &records[record_count];
    kstrlcpy(record->label, label, BOOT_LABEL_CAP);
    record->tick = timer_ticks();
    record->result = (u8)result;
    return record_count++;
}

int boot_status_count(void) { return record_count; }

const boot_record_t *boot_status_get(int index) {
    if (index < 0 || index >= record_count) return 0;
    return &records[index];
}

const char *boot_result_name(enum boot_result result) {
    switch (result) {
        case BOOT_RESULT_OK: return "OK";
        case BOOT_RESULT_WARN: return "WARN";
        case BOOT_RESULT_FAIL: return "FAIL";
        default: return "UNKNOWN";
    }
}
