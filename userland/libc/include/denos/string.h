#ifndef DENOS_LIBC_STRING_H
#define DENOS_LIBC_STRING_H
#include <stddef.h>
size_t den_strlen(const char*); int den_strcmp(const char*,const char*); int den_strncmp(const char*,const char*,size_t); char*den_strchr(const char*,int); char*den_strstr(const char*,const char*); size_t den_strlcpy(char*,const char*,size_t);
#endif
