#include <denos/base64.h>
#include <stddef.h>
static const char tab[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
size_t den_b64_encode(const unsigned char*s,size_t n,char*d,size_t cap){size_t need=((n+2)/3)*4;if(!d||cap<=need)return 0;size_t o=0;for(size_t i=0;i<n;i+=3){unsigned v=(unsigned)s[i]<<16;if(i+1<n)v|=(unsigned)s[i+1]<<8;if(i+2<n)v|=s[i+2];d[o++]=tab[(v>>18)&63];d[o++]=tab[(v>>12)&63];d[o++]=i+1<n?tab[(v>>6)&63]:'=';d[o++]=i+2<n?tab[v&63]:'=';}d[o]=0;return o;}
