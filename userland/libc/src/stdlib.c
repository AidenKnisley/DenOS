#include <denos/stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
long den_strtol(const char*s,char**end,int base){long v;errno=0;v=strtol(s,end,base);return v;}
int den_atoi(const char*s){long v=den_strtol(s,NULL,10);if(errno||v>INT_MAX||v<INT_MIN)return 0;return(int)v;}
int den_abs(int x){if(x==INT_MIN)return INT_MAX;return x<0?-x:x;}
void den_qsort(void*b,size_t n,size_t z,int(*cmp)(const void*,const void*)){unsigned char*t=b,tmp[256];if(!b||!cmp||!z||z>sizeof tmp)return;for(size_t i=1;i<n;i++){size_t j=i;while(j&&cmp(t+(j-1)*z,t+j*z)>0){for(size_t k=0;k<z;k++)tmp[k]=t[(j-1)*z+k];for(size_t k=0;k<z;k++)t[(j-1)*z+k]=t[j*z+k];for(size_t k=0;k<z;k++)t[j*z+k]=tmp[k];j--;}}}
