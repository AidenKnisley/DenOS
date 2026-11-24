#include "service.h"
#include "kstring.h"
typedef struct service_slot { service_info_t info; scheduler_entry_t entry; void *context; } service_slot_t;
static service_slot_t slots[SERVICE_MAX];
static u32 next_id;
static service_slot_t *find(u32 id) { if(!id) return 0; for(usize i=0;i<SERVICE_MAX;i++) if(slots[i].info.id==id) return &slots[i]; return 0; }
int service_init(void) { kmemset(slots,0,sizeof(slots));next_id=1;return 0; }
int service_register(const char *name,u8 priority,u32 interval,scheduler_entry_t entry,void *context,u32 *id) {
    if(!name||!name[0]||kstrnlen(name,SERVICE_NAME_CAP)>=SERVICE_NAME_CAP||priority>7u||!interval||!entry) return -1;
    for(usize i=0;i<SERVICE_MAX;i++) if(!slots[i].info.id) { service_slot_t *s=&slots[i];kmemset(s,0,sizeof(*s));s->info.id=next_id++;if(!next_id) next_id=1;kstrlcpy(s->info.name,name,sizeof(s->info.name));s->info.priority=priority;s->info.interval=interval;s->entry=entry;s->context=context;if(id)*id=s->info.id;return 0; }
    return -1;
}
int service_start(u32 id) { service_slot_t *s=find(id);if(!s)return -1; if(s->info.running) return 0;
    if(s->info.task_id) { scheduler_task_info_t task; if(scheduler_get(s->info.task_id,&task)==0&&task.state==SCHEDULER_STOPPED) { if(scheduler_restart(s->info.task_id)<0)return -1; s->info.running=1;++s->info.starts;return 0; } }
    if(scheduler_create(s->info.name,s->info.priority,s->info.interval,s->entry,s->context,&s->info.task_id)<0)return -1;
    s->info.running=1;++s->info.starts;return 0;
}
int service_stop(u32 id) { service_slot_t *s=find(id);if(!s||!s->info.running)return -1;if(scheduler_stop(s->info.task_id)<0)return -1;s->info.running=0;return 0; }
int service_get(u32 id,service_info_t *out) { service_slot_t *s=find(id);if(!s||!out)return -1; scheduler_task_info_t task;if(scheduler_get(s->info.task_id,&task)==0)s->info.running=(task.state==SCHEDULER_READY||task.state==SCHEDULER_SLEEPING);*out=s->info;return 0; }
int service_get_by_index(usize index,service_info_t *out) { usize n=0;if(!out)return -1;for(usize i=0;i<SERVICE_MAX;i++)if(slots[i].info.id){if(n++==index)return service_get(slots[i].info.id,out);}return -1; }
usize service_count(void) { usize n=0;for(usize i=0;i<SERVICE_MAX;i++)if(slots[i].info.id)++n;return n; }
