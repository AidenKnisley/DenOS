#include <denos/path.h>
#include <string.h>
int den_path_is_safe(const char*p){if(!p||!*p)return 0;const char*s=p;while(*s){while(*s=='/')s++;const char*b=s;while(*s&&*s!='/')s++;size_t n=(size_t)(s-b);if(n==2&&b[0]=='.'&&b[1]=='.')return 0;}return 1;}
const char*den_path_basename(const char*p){const char*r=p;if(!p)return "";for(;*p;p++)if(*p=='/'&&p[1])r=p+1;return r;}
