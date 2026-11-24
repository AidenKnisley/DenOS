#ifndef DENOS_ENV_H
#define DENOS_ENV_H
#include "types.h"
#define ENV_MAX_ENTRIES 24u
#define ENV_KEY_CAP 24u
#define ENV_VALUE_CAP 96u
typedef struct env_entry { char key[ENV_KEY_CAP]; char value[ENV_VALUE_CAP]; } env_entry_t;
void env_init(void);
int env_set(const char *key,const char *value);
int env_unset(const char *key);
const char *env_get(const char *key);
usize env_count(void);
int env_get_by_index(usize index,env_entry_t *entry);
#endif
