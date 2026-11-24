#ifndef DENOS_LIBC_VECTOR_H
#define DENOS_LIBC_VECTOR_H
#include <stddef.h>
typedef struct{void*data;size_t len,cap,elem;}den_vec; int den_vec_init(den_vec*,size_t);void den_vec_free(den_vec*);int den_vec_push(den_vec*,const void*);void*den_vec_at(den_vec*,size_t);
#endif
