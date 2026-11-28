#include <stdio.h>
#include <string.h>
#include <ctype.h>

int main(void) {
    char title[48];
    char suffix[16];

    strcpy(title, "DenOS");
    strcpy(suffix, " C runtime");
    strcat(title, suffix);

    printf("%s\n", title);
    printf("length=%u compare=%d upper=%c digit=%d\n",
           strlen(title), strcmp(title, "DenOS C runtime"),
           toupper('c'), isdigit('9'));

    return strstr(title, "runtime") != 0 ? 0 : 1;
}
