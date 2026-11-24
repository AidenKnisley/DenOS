#include <denos/string.h>
#include <stddef.h>
size_t den_strlen(const char*s){size_t n=0;if(!s)return 0;while(s[n])n++;return n;}
int den_strcmp(const char*a,const char*b){if(!a||!b)return a==b?0:(a?1:-1);while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int den_strncmp(const char*a,const char*b,size_t n){while(n&&*a&&*a==*b){a++;b++;n--;}return n?(unsigned char)*a-(unsigned char)*b:0;}
char*den_strchr(const char*s,int c){if(!s)return NULL;do{if((unsigned char)*s==(unsigned char)c)return(char*)s;}while(*s++);return NULL;}
char*den_strstr(const char*h,const char*n){size_t k;if(!h||!n)return NULL;if(!*n)return(char*)h;k=den_strlen(n);for(;*h;h++)if(*h==*n&&den_strncmp(h,n,k)==0)return(char*)h;return NULL;}
size_t den_strlcpy(char*d,const char*s,size_t cap){size_t n=den_strlen(s),i=0;if(d&&cap){while(i+1<cap&&s[i]){d[i]=s[i];i++;}d[i]=0;}return n;}
