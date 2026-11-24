#include <denos/format.h>
#include <stddef.h>
static void put(char*d,size_t c,size_t*i,char x){if(c&&*i+1<c)d[*i]=x;(*i)++;}
int den_itoa(long long v,unsigned base,int upper,char*d,size_t c){char t[66];const char*digits=upper?"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ":"0123456789abcdefghijklmnopqrstuvwxyz";size_t n=0,o=0;unsigned long long x;if(!d||c==0||base<2||base>36)return-1;if(v<0&&base==10){put(d,c,&o,'-');x=(unsigned long long)(-(v+1))+1;}else x=(unsigned long long)v;do{t[n++]=digits[x%base];x/=base;}while(x);while(n)put(d,c,&o,t[--n]);d[o<c?o:c-1]=0;return(int)o;}
