#ifndef DENOS_SERVICE_H
#define DENOS_SERVICE_H
#include "types.h"
#include "scheduler.h"
#define SERVICE_MAX 16u
#define SERVICE_NAME_CAP 24u
typedef struct service_info { u32 id; u32 task_id; char name[SERVICE_NAME_CAP]; u8 running; u8 priority; u32 interval; u64 starts; } service_info_t;
int service_init(void);
int service_register(const char *name,u8 priority,u32 interval,scheduler_entry_t entry,void *context,u32 *service_id);
int service_start(u32 service_id);
int service_stop(u32 service_id);
int service_get(u32 service_id,service_info_t *out);
int service_get_by_index(usize index,service_info_t *out);
usize service_count(void);
#endif
