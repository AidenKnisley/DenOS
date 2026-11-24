#include <denos/hashmap.h>
#include <stdlib.h>
#include <string.h>
static unsigned long hkey(const char*s){unsigned long h=1469598103934665603ull;while(*s){h^=(unsigned char)*s++;h*=1099511628211ull;}return h;}
int den_map_init(den_map*m,size_t cap){if(!m)return-1;if(cap<8)cap=8;m->slots=calloc(cap,sizeof(*m->slots));if(!m->slots)return-1;m->cap=cap;m->len=0;return 0;}
void den_map_free(den_map*m){if(!m)return;for(size_t i=0;i<m->cap;i++)free(m->slots[i].key);free(m->slots);m->slots=NULL;m->cap=m->len=0;}
int den_map_put(den_map*m,const char*k,void*v){if(!m||!k||!m->slots)return-1;size_t i=hkey(k)%m->cap;for(size_t n=0;n<m->cap;n++,i=(i+1)%m->cap){if(!m->slots[i].key||strcmp(m->slots[i].key,k)==0){if(!m->slots[i].key){m->slots[i].key=strdup(k);if(!m->slots[i].key)return-1;m->len++;}m->slots[i].value=v;return 0;}}return-1;}
void*den_map_get(const den_map*m,const char*k){if(!m||!k||!m->slots)return NULL;size_t i=hkey(k)%m->cap;for(size_t n=0;n<m->cap;n++,i=(i+1)%m->cap){if(!m->slots[i].key)return NULL;if(strcmp(m->slots[i].key,k)==0)return m->slots[i].value;}return NULL;}
