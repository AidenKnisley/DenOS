#ifndef DENOS_RTC_H
#define DENOS_RTC_H

#include "types.h"

typedef struct rtc_datetime {
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
} rtc_datetime_t;

/* Returns 0 on a valid CMOS RTC sample and -1 on timeout/invalid fields. */
int rtc_read(rtc_datetime_t *result);

#endif
