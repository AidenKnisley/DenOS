#include <denos/vector.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int den_vec_init(den_vec*v,size_t z){if(!v||!z)return-1;v->data=NULL;v->len=0;v->cap=0;v->elem=z;return 0;}
void den_vec_free(den_vec*v){if(v){free(v->data);v->data=NULL;v->len=v->cap=0;}}
int den_vec_push(den_vec*v,const void*x){if(!v||!x)return-1;if(v->len==v->cap){size_t c=v->cap?v->cap*2:8;if(c<v->cap||c>SIZE_MAX/v->elem)return-1;void*p=realloc(v->data,c*v->elem);if(!p)return-1;v->data=p;v->cap=c;}memcpy((char*)v->data+v->len*v->elem,x,v->elem);v->len++;return 0;}
void*den_vec_at(den_vec*v,size_t i){return v&&i<v->len?(char*)v->data+i*v->elem:NULL;}
