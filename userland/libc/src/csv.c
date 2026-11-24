#include <denos/csv.h>
#include <stddef.h>
int den_csv_next(const char**p,char*out,size_t cap){if(!p||!*p||!out||!cap)return-1;const char*s=*p;size_t n=0;int q=0;if(*s=='"'){q=1;s++;}while(*s){if(q&&*s=='"'&&s[1]=='"'){if(n+1>=cap)return-1;out[n++]='"';s+=2;continue;}if(q&&*s=='"'){s++;q=0;break;}if(!q&&(*s==','||*s=='\n'))break;if(n+1>=cap)return-1;out[n++]=*s++;}if(q)return-1;out[n]=0;if(*s==',')s++;else if(*s=='\n')s++;*p=s;return(int)n;}
