#ifndef DENOS_HISTORY_H
#define DENOS_HISTORY_H
#include "types.h"
#define HISTORY_CAP 24u
#define HISTORY_LINE_CAP 96u
void history_init(void);
int history_add(const char *line);
usize history_count(void);
int history_get_newest(usize offset,char *output,usize capacity);
#endif
