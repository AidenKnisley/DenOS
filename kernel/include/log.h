#ifndef DENOS_LOG_H
#define DENOS_LOG_H

#include "types.h"
#include <stdarg.h>

enum klog_level {
    KLOG_DEBUG = 0,
    KLOG_INFO = 1,
    KLOG_WARN = 2,
    KLOG_ERROR = 3
};

void klog_init(void);
void klog_set_level(enum klog_level level);
void klog_write(enum klog_level level, const char *message);
void klogf(enum klog_level level, const char *format, ...);
const char *klog_level_name(enum klog_level level);

#endif
