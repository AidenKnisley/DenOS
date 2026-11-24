#include <denos/json.h>
#include <stddef.h>
static void ws(const char**p){while(**p==' '||**p=='\t'||**p=='\r'||**p=='\n')(*p)++;}
int den_json_string(const char**p,char*out,size_t cap){if(!p||!out||!cap)return-1;ws(p);if(*(*p)++!='"')return-1;size_t n=0;while(**p&&**p!='"'){char c=*(*p)++;if(c=='\\'){c=*(*p)++;if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c!='"'&&c!='\\'&&c!='/')return-1;}if(n+1>=cap)return-1;out[n++]=c;}if(**p!='"')return-1;(*p)++;out[n]=0;return(int)n;}
