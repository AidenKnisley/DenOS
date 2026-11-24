#include <denos/ctype.h>
int den_isalpha(int c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z');}
int den_isdigit(int c){return c>='0'&&c<='9';}
int den_isalnum(int c){return den_isalpha(c)||den_isdigit(c);}
int den_isspace(int c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';}
int den_isxdigit(int c){return den_isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
int den_tolower(int c){return c>='A'&&c<='Z'?c+('a'-'A'):c;}
int den_toupper(int c){return c>='a'&&c<='z'?c-('a'-'A'):c;}
