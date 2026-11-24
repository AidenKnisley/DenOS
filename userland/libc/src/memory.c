#include <denos/memory.h>
#include <stdint.h>
void*den_memset(void*d,int v,size_t n){unsigned char*p=d;while(n--)*p++=(unsigned char)v;return d;}
void*den_memcpy(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
void*den_memmove(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;if((uintptr_t)a<(uintptr_t)b){for(size_t i=0;i<n;i++)a[i]=b[i];}else if(a!=b){while(n){n--;a[n]=b[n];}}return d;}
int den_memcmp(const void*a,const void*b,size_t n){const unsigned char*x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return(int)x[i]-(int)y[i];return 0;}
