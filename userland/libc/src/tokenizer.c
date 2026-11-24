#include <denos/tokenizer.h>
#include <string.h>
size_t den_split(char*s,char delim,char**out,size_t cap){size_t n=0;if(!s||!out||!cap)return 0;char*p=s;while(n<cap){out[n++]=p;char*q=strchr(p,delim);if(!q)break;*q=0;p=q+1;}return n;}
