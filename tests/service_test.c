#include <stdio.h>
#include <string.h>
#include "service.h"
static u32 next_task=10;
static scheduler_task_info_t task;
int scheduler_create(const char *name,u8 priority,u32 interval,scheduler_entry_t entry,void *context,u32 *id) {
    (void)entry;(void)context;memset(&task,0,sizeof(task));task.id=next_task++;task.state=SCHEDULER_READY;task.priority=priority;task.interval=interval;strncpy(task.name,name,sizeof(task.name)-1);*id=task.id;return 0;
}
int scheduler_stop(u32 id) { if(id!=task.id||task.state!=SCHEDULER_READY)return -1;task.state=SCHEDULER_STOPPED;return 0; }
int scheduler_restart(u32 id) { if(id!=task.id||task.state!=SCHEDULER_STOPPED)return -1;task.state=SCHEDULER_READY;return 0; }
int scheduler_get(u32 id,scheduler_task_info_t *out) { if(id!=task.id||!out)return -1;*out=task;return 0; }
static int entry(void *context){(void)context;return SCHEDULER_TASK_REPEAT;}
int main(void) { u32 id;service_info_t info;if(service_init()<0)return 1;if(service_register("watchdog",2,100,entry,0,&id)<0)return 2;if(service_start(id)<0||service_start(id)<0)return 3;if(service_get(id,&info)<0||!info.running||info.starts!=1)return 4;if(service_stop(id)<0||service_stop(id)==0)return 5;if(service_get(id,&info)<0||info.running)return 6;if(service_start(id)<0||service_get(id,&info)<0||!info.running||info.starts!=2)return 7;puts("Service manager tests passed.");return 0; }
