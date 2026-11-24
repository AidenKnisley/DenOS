#include "env.h"
#include "kstring.h"
static env_entry_t entries[ENV_MAX_ENTRIES];
static int valid_key(const char *key) { if(!key||!key[0]||kstrnlen(key,ENV_KEY_CAP)>=ENV_KEY_CAP) return 0; for(usize i=0;key[i];i++) if(!((key[i]>='A'&&key[i]<='Z')||(key[i]>='a'&&key[i]<='z')||(key[i]>='0'&&key[i]<='9')||key[i]=='_')) return 0; return 1; }
void env_init(void) { kmemset(entries,0,sizeof(entries)); (void)env_set("USER","local-user"); (void)env_set("HOME","/home/user"); (void)env_set("HOSTNAME","denos"); (void)env_set("SHELL","/bin/sh"); (void)env_set("PATH","/bin:/sbin"); }
const char *env_get(const char *key) { if(!valid_key(key)) return 0; for(usize i=0;i<ENV_MAX_ENTRIES;i++) if(entries[i].key[0]&&kstrcmp(entries[i].key,key)==0) return entries[i].value; return 0; }
int env_set(const char *key,const char *value) { if(!valid_key(key)||!value||kstrnlen(value,ENV_VALUE_CAP)>=ENV_VALUE_CAP) return -1; usize free_slot=ENV_MAX_ENTRIES; for(usize i=0;i<ENV_MAX_ENTRIES;i++) { if(entries[i].key[0]&&kstrcmp(entries[i].key,key)==0) { kstrlcpy(entries[i].value,value,sizeof(entries[i].value));return 0; } if(!entries[i].key[0]&&free_slot==ENV_MAX_ENTRIES) free_slot=i; } if(free_slot==ENV_MAX_ENTRIES) return -1; kstrlcpy(entries[free_slot].key,key,sizeof(entries[free_slot].key));kstrlcpy(entries[free_slot].value,value,sizeof(entries[free_slot].value));return 0; }
int env_unset(const char *key) { if(!valid_key(key)) return -1; for(usize i=0;i<ENV_MAX_ENTRIES;i++) if(entries[i].key[0]&&kstrcmp(entries[i].key,key)==0) { kmemset(&entries[i],0,sizeof(entries[i]));return 0; } return -1; }
usize env_count(void) { usize n=0; for(usize i=0;i<ENV_MAX_ENTRIES;i++) if(entries[i].key[0]) ++n; return n; }
int env_get_by_index(usize index,env_entry_t *out) { usize n=0; if(!out) return -1; for(usize i=0;i<ENV_MAX_ENTRIES;i++) if(entries[i].key[0]) { if(n++==index) { *out=entries[i];return 0; } } return -1; }
