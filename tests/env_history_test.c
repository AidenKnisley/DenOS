#include <stdio.h>
#include <string.h>
#include "env.h"
#include "history.h"
int main(void) { char out[HISTORY_LINE_CAP];env_init();if(strcmp(env_get("HOME"),"/home/user")!=0)return 1;if(env_set("FOO","bar")<0||strcmp(env_get("FOO"),"bar")!=0)return 2;if(env_unset("FOO")<0||env_get("FOO"))return 3;if(env_set("BAD-KEY","x")==0)return 4;history_init();history_add("help");history_add("sysinfo");if(history_get_newest(0,out,sizeof(out))<0||strcmp(out,"sysinfo")!=0)return 5;if(history_get_newest(1,out,sizeof(out))<0||strcmp(out,"help")!=0)return 6;puts("Environment and history tests passed.");return 0; }
