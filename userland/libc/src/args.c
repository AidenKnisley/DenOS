#include <denos/args.h>
#include <string.h>
int den_arg_find(int argc,const char*const argv[],const char*name){if(!argv||!name)return-1;for(int i=1;i<argc;i++)if(argv[i]&&strcmp(argv[i],name)==0)return i;return-1;}
const char*den_arg_value(int argc,const char*const argv[],const char*name){int i=den_arg_find(argc,argv,name);return i>=0&&i+1<argc?argv[i+1]:0;}
