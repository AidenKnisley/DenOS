#include <denos/ini.h>
#include <string.h>
#include <ctype.h>
static char*trim(char*s){while(isspace((unsigned char)*s))s++;char*e=s+strlen(s);while(e>s&&isspace((unsigned char)e[-1]))*--e=0;return s;}
int den_ini_get(const char*text,const char*section,const char*key,char*out,size_t cap){if(!text||!section||!key||!out||!cap)return-1;char sec[96]="";const char*p=text;while(*p){char line[256];size_t n=0;while(*p&&*p!='\n'&&n+1<sizeof line)line[n++]=*p++;if(*p=='\n')p++;line[n]=0;char*s=trim(line);if(!*s||*s==';'||*s=='#')continue;if(*s=='['){char*e=strchr(s,']');if(e){*e=0;strncpy(sec,trim(s+1),sizeof sec-1);sec[sizeof sec-1]=0;}continue;}char*eq=strchr(s,'=');if(eq&&strcmp(sec,section)==0){*eq++=0;if(strcmp(trim(s),key)==0){char*v=trim(eq);size_t z=strlen(v);if(z>=cap)return-1;memcpy(out,v,z+1);return 0;}}}return 1;}
