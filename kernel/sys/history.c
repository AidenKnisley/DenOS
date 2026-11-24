#include "history.h"
#include "kstring.h"
static char lines[HISTORY_CAP][HISTORY_LINE_CAP];
static usize newest;
static usize count;
void history_init(void) { kmemset(lines,0,sizeof(lines));newest=0;count=0; }
int history_add(const char *line) { if(!line||!line[0]||kstrnlen(line,HISTORY_LINE_CAP)>=HISTORY_LINE_CAP) return -1; if(count&&kstrcmp(lines[newest],line)==0) return 0; newest=(newest+1u)%HISTORY_CAP;kstrlcpy(lines[newest],line,HISTORY_LINE_CAP);if(count<HISTORY_CAP) ++count;return 0; }
usize history_count(void) { return count; }
int history_get_newest(usize offset,char *out,usize cap) { if(!out||!cap||offset>=count) return -1; usize index=(newest+HISTORY_CAP-(offset%HISTORY_CAP))%HISTORY_CAP; if(kstrnlen(lines[index],HISTORY_LINE_CAP)>=cap) return -1; kstrlcpy(out,lines[index],cap);return 0; }
