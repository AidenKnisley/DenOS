#include "serial.h"
#include "fs.h"
#include "types.h"
#include "timer.h"
#include "memory.h"
#include "cvm.h"

void editor_open(const char *name);

static char compiled_image[CVM_IMAGE_CAP];

static int eq(const char *a, const char *b) {
    int i=0; while(a[i]&&b[i]&&a[i]==b[i]) i++; return a[i]==b[i];
}
static int starts(const char *a, const char *b) {
    while(*b){if(*a++!=*b++)return 0;}return 1;
}
static void puts(const char *s){serial_write(s);}
static void number(u64 n){char b[24];int i=0;if(!n){serial_putc('0');return;}while(n&&i<23){b[i++]=(char)('0'+n%10);n/=10;}while(i)serial_putc(b[--i]);}
static void help(void) {
    puts("DenOS commands:\n"
         "  help clear echo uname hostname pwd ls cat write touch rm vi/vim\n"
         "  cc source.c -o program   compile C into an AEX executable\n"
         "  ./program                run a compiled program\n"
         "  about uptime free meminfo df guess ttt reboot halt\n");
}
static void game_guess(void){char b[32];puts("Guess a number from 1 to 5: ");serial_readline(b,sizeof b);if(b[0]=='3'&&b[1]==0)puts("Correct!\n");else puts("Nope, it was 3.\n");}
static void game_ttt(void){puts("Compile the full game with: cc ttt.c -o ttt\nThen run: ./ttt\n");}
static void skip_spaces(const char **p){while(**p==' '||**p=='\t')(*p)++;}
static int word(const char **p,char *out,usize cap){skip_spaces(p);usize i=0;while(**p&&**p!=' '&&**p!='\t'){if(i+1<cap)out[i++]=**p;(*p)++;}out[i]=0;return i!=0;}
static void command_cc(const char *args) {
    char source_name[64], option[16], output_name[64], executable_name[68];
    const char *p=args;
    if(!word(&p,source_name,sizeof source_name)||!word(&p,option,sizeof option)||!eq(option,"-o")||!word(&p,output_name,sizeof output_name)){
        puts("usage: cc source.c -o program\n");return;
    }
    skip_spaces(&p);if(*p){puts("cc: unexpected extra arguments\n");return;}
    const char *source=fs_read(source_name);
    if(!source){puts("cc: source file not found: ");puts(source_name);puts("\n");return;}
    int size=c_compile(source,compiled_image,sizeof compiled_image);
    if(size<0){puts("cc: ");puts(c_compile_error());puts("\n");return;}
    usize n=0;while(output_name[n]&&n<60){executable_name[n]=output_name[n];n++;}
    executable_name[n]=0;
    if(n<3||executable_name[n-3]!='.'||executable_name[n-2]!='a'||executable_name[n-1]!='x'){
        executable_name[n++]='.';executable_name[n++]='a';executable_name[n++]='x';executable_name[n]=0;
    }
    if(fs_write(executable_name,compiled_image)<0){puts("cc: cannot save executable (filesystem full or disk write error)\n");return;}
    puts("compiled ");puts(source_name);puts(" -> ");puts(executable_name);puts(" (AEX bytecode, ");number((u64)size);puts(" bytes)\n");
}
static void run_program(const char *name) {
    int result=program_run(name);
    if(result>=0){puts("[program exited with code ");number((u64)result);puts("]\n");}
}
static void command(const char *line){
    if(eq(line,"help")){help();return;}
    if(eq(line,"clear")){puts("\033[2J\033[H");return;}
    if(eq(line,"uname")){puts("DenOS denos 0.3 x86_64 custom-kernel\n");return;}
    if(eq(line,"hostname")){puts("denos\n");return;}
    if(eq(line,"pwd")){puts("/\n");return;}
    if(eq(line,"ls")){fs_list();return;}
    if(eq(line,"about")){puts("DenOS 0.3 - independent 64-bit OS with an experimental C-subset compiler and AEX bytecode VM.\n");return;}
    if(eq(line,"uptime")){puts("Uptime: ");number(timer_ticks()/100);puts(" seconds (PIT estimate)\n");return;}
    if(eq(line,"free")||eq(line,"meminfo")){puts("Bootstrap heap: ");number(memory_used());puts(" / ");number(memory_capacity());puts(" bytes used\n");return;}
    if(eq(line,"df")){puts("Filesystem  Type  Files  Data\nrootfs      ");puts(fs_is_persistent()?"ata-pio ":"ramfs    ");number(fs_file_count());puts("      ");number(fs_bytes_used());puts(" bytes\n");return;}
    if(eq(line,"halt")){puts("System halted. You may close QEMU.\n");__asm__ volatile("cli");for(;;)__asm__ volatile("hlt");}
    if(eq(line,"reboot")){puts("Rebooting...\n");__asm__ volatile("cli; mov $0xFE,%al; outb %al,$0x64");for(;;)__asm__ volatile("hlt");}
    if(eq(line,"cc")){puts("usage: cc source.c -o program\n");return;}
    if(starts(line,"cc ")){command_cc(line+3);return;}
    if(starts(line,"./")){run_program(line+2);return;}
    if(starts(line,"echo ")){puts(line+5);puts("\n");return;}
    if(starts(line,"cat ")){const char*s=fs_read(line+4);if(s)puts(s);else puts("cat: file not found\n");return;}
    if(starts(line,"vim ")){editor_open(line+4);return;}
    if(starts(line,"vi ")){editor_open(line+3);return;}
    if(starts(line,"touch ")){if(fs_touch(line+6)==0)puts("");else puts("touch: cannot create file\n");return;}
    if(starts(line,"rm ")){if(fs_remove(line+3)==0)puts("");else puts("rm: file not found\n");return;}
    if(starts(line,"write ")){char tmp[64];int i=0;const char*p=line+6;while(*p&&*p!=' '&&i<63)tmp[i++]=*p++;tmp[i]=0;if(*p==' '){p++;if(fs_write(tmp,p)==0)puts("written\n");else puts("write: failed (name/data too long or filesystem full)\n");}else puts("usage: write filename text\n");return;}
    if(eq(line,"guess")){game_guess();return;}if(eq(line,"ttt")){game_ttt();return;}
    puts("command not found: ");puts(line);puts(". Type 'help'.\n");
}
void shell_run(void){char line[256];for(;;){puts("aiden@denos:/$ ");serial_readline(line,sizeof line);if(line[0])command(line);}}
