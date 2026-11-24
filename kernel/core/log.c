#include "log.h"
#include "serial.h"
#include "timer.h"
#include "kformat.h"

static enum klog_level minimum_level = KLOG_INFO;
static int log_ready;

const char *klog_level_name(enum klog_level level) {
    switch (level) {
        case KLOG_DEBUG: return "DEBUG";
        case KLOG_INFO: return "INFO ";
        case KLOG_WARN: return "WARN ";
        case KLOG_ERROR: return "ERROR";
        default: return "?????";
    }
}

void klog_init(void) {
    minimum_level = KLOG_INFO;
    log_ready = 1;
}

void klog_set_level(enum klog_level level) {
    if (level < KLOG_DEBUG) level = KLOG_DEBUG;
    if (level > KLOG_ERROR) level = KLOG_ERROR;
    minimum_level = level;
}

void klog_write(enum klog_level level, const char *message) {
    char prefix[48];
    if (level < minimum_level || !message) return;
    if (!log_ready) {
        serial_write(message);
        serial_putc('\n');
        return;
    }
    ksnprintf(prefix, sizeof(prefix), "[%08llu] [%s] ",
              (unsigned long long)timer_ticks(), klog_level_name(level));
    serial_write(prefix);
    serial_write(message);
    serial_putc('\n');
}

void klogf(enum klog_level level, const char *format, ...) {
    char message[256];
    va_list args;
    if (level < minimum_level) return;
    va_start(args, format);
    kvsnprintf(message, sizeof(message), format, args);
    va_end(args);
    klog_write(level, message);
}
