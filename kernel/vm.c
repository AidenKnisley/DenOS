#include "cvm.h"
#include "serial.h"
#include "fs.h"
#include "types.h"

#define VM_CODE_MAX 768
#define VM_STR_MAX 2048
#define VM_STACK_MAX 2048
#define VM_MEM_MAX 4096
#ifndef VM_INSTRUCTION_BUDGET
#define VM_INSTRUCTION_BUDGET 50000000u
#endif
#define TAG_STRING ((u64)1<<62)
#define TAG_ADDRESS ((u64)1<<61)

typedef struct { u8 op; i32 arg; } VMInstruction;
static VMInstruction vm_code[VM_CODE_MAX];
static char vm_strings[VM_STR_MAX + 1];
static i64 vm_stack[VM_STACK_MAX];
static i64 vm_memory[VM_MEM_MAX];
static int vm_sp;
static int vm_pc;
static int vm_fault;
static int vm_string_count;

static int hex_value(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='A'&&c<='F')return c-'A'+10;
    if(c>='a'&&c<='f')return c-'a'+10;
    return -1;
}
static int read_hex_byte(const char **p, u8 *out) {
    if (!p || !*p || !(*p)[0] || !(*p)[1]) return -1;
    int a=hex_value((*p)[0]),b=hex_value((*p)[1]);
    if(a<0||b<0)return -1;
    *out=(u8)((a<<4)|b);*p+=2;return 0;
}
static void push(i64 v) {
    if (vm_fault) return;
    if(vm_sp<0 || vm_sp>=VM_STACK_MAX){serial_write("AEX: stack overflow\n");vm_fault=1;return;}
    vm_stack[vm_sp++]=v;
}
static i64 pop(void) {
    if (vm_fault) return 0;
    if(vm_sp<=0 || vm_sp>VM_STACK_MAX){serial_write("AEX: stack underflow\n");vm_fault=1;return 0;}
    return vm_stack[--vm_sp];
}
static void print_i64(i64 value) {
    char b[24];int n=0;u64 v;
    if(value<0){serial_putc('-');v=(u64)(-(value+1))+1;}else v=(u64)value;
    if(!v){serial_putc('0');return;}
    while(v&&n<23){b[n++]=(char)('0'+v%10);v/=10;}
    while(n)serial_putc(b[--n]);
}
static const char *string_arg(i64 v) {
    if(((u64)v&TAG_STRING)!=0){
        u64 off=(u64)v&0xffff;
        return off<(u64)vm_string_count?vm_strings+off:"";
    }
    return "";
}
static int pointer_slot(i64 v) {
    if(((u64)v&TAG_ADDRESS)==0)return -1;
    u64 s=(u64)v&0xffff;
    return s<VM_MEM_MAX?(int)s:-1;
}
static int builtin_printf(int base,int argc) {
    const char *fmt=string_arg(vm_stack[base]);int arg=base+1;
    while(*fmt) {
        if(*fmt!='%'){serial_putc(*fmt++);continue;}
        fmt++;if(*fmt=='%'){serial_putc('%');fmt++;continue;}
        char spec=*fmt?*fmt++:0;
        if(arg>=base+argc)break;
        i64 v=vm_stack[arg++];
        if(spec=='d'||spec=='i')print_i64(v);
        else if(spec=='c')serial_putc((char)v);
        else if(spec=='s')serial_write(string_arg(v));
        else {serial_putc('%');if(spec)serial_putc(spec);}
    }
    return 0;
}
static int is_ws(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static int builtin_scanf(int base,int argc) {
    if(argc<2)return 0;
    const char *fmt=string_arg(vm_stack[base]);
    char input[128];serial_readline(input,sizeof(input));
    int ip=0,ap=base+1,converted=0;
    while(*fmt&&ap<base+argc) {
        if(is_ws(*fmt)){while(is_ws(*fmt))fmt++;while(is_ws(input[ip]))ip++;continue;}
        if(*fmt!='%'){if(input[ip]==*fmt){ip++;fmt++;continue;}break;}
        fmt++;char spec=*fmt?*fmt++:0;int slot=pointer_slot(vm_stack[ap++]);
        if(slot<0)break;
        if(spec=='d'||spec=='i') {
            while(is_ws(input[ip]))ip++;
            int sign=1;if(input[ip]=='-'){sign=-1;ip++;}else if(input[ip]=='+')ip++;
            if(input[ip]<'0'||input[ip]>'9')break;
            const u64 limit = sign < 0
                ? 0x8000000000000000ULL
                : 0x7FFFFFFFFFFFFFFFULL;
            u64 value = 0;
            int overflow = 0;
            while(input[ip]>='0'&&input[ip]<='9') {
                u64 digit = (u64)(input[ip++] - '0');
                if (value > (limit - digit) / 10u) {
                    overflow = 1;
                    while (input[ip]>='0'&&input[ip]<='9') ip++;
                    break;
                }
                value = value * 10u + digit;
            }
            if (overflow) break;
            if (sign < 0) {
                vm_memory[slot] = value == 0x8000000000000000ULL
                    ? (-9223372036854775807LL - 1LL)
                    : -(i64)value;
            } else {
                vm_memory[slot] = (i64)value;
            }
            converted++;
        } else if(spec=='c') {
            if(!input[ip])break;
            vm_memory[slot]=(u8)input[ip++];
            converted++;
        } else break;
    }
    return converted;
}
static i64 call_builtin(int encoded) {
    int id=(encoded>>16)&0xffff,argc=encoded&0xffff,base=vm_sp-argc;
    if (vm_fault) return 0;
    if(argc<0 || argc>VM_STACK_MAX || base<0 || id<1 || id>5){
        serial_write("AEX: invalid built-in call\n"); vm_fault=1; return 0;
    }
    if ((id==2 || id==3) && argc!=1) { vm_fault=1; return 0; }
    if (id==4 && argc<2) { vm_fault=1; return 0; }
    if (id==5 && argc!=0) { vm_fault=1; return 0; }
    if (id==1 && argc<1) { vm_fault=1; return 0; }
    i64 result=0;
    if(id==1)result=builtin_printf(base,argc);
    else if(id==2){serial_write(string_arg(vm_stack[base]));serial_putc('\n');}
    else if(id==3)serial_putc((char)vm_stack[base]);
    else if(id==4)result=builtin_scanf(base,argc);
    else if(id==5)result=(u8)serial_getc();
    else serial_write("AEX: unsupported built-in\n");
    if (!vm_fault) { vm_sp=base; push(result); }
    return result;
}
int c_execute(const char *image) {
    if(!image||image[0]!='A'||image[1]!='E'||image[2]!='X'||image[3]!='1'){
        serial_write("exec: not an Aiden executable\n");return -1;
    }
    const char *p=image+4;u8 h[4];
    for(int i=0;i<4;i++)if(read_hex_byte(&p,&h[i])<0){serial_write("exec: corrupt header\n");return -1;}
    int code_count=((int)h[0]<<8)|h[1],string_count=((int)h[2]<<8)|h[3];
    if(code_count<1||code_count>VM_CODE_MAX||string_count<0||string_count>VM_STR_MAX){serial_write("exec: invalid executable size\n");return -1;}
    for(int i=0;i<code_count;i++) {
        u8 b[5];for(int j=0;j<5;j++)if(read_hex_byte(&p,&b[j])<0){serial_write("exec: truncated bytecode\n");return -1;}
        vm_code[i].op=b[0];vm_code[i].arg=(i32)((u32)b[1]|((u32)b[2]<<8)|((u32)b[3]<<16)|((u32)b[4]<<24));
    }
    for(int i=0;i<string_count;i++){u8 b;if(read_hex_byte(&p,&b)<0){serial_write("exec: truncated strings\n");return -1;}vm_strings[i]=(char)b;}
    vm_strings[string_count]=0; /* Bound string reads for malformed images. */
    vm_string_count=string_count;

    /* Reject malformed bytecode before interpreting any of it. */
    for (int i=0;i<code_count;i++) {
        int op=vm_code[i].op, arg=vm_code[i].arg;
        if (op<1 || op>27) { serial_write("exec: invalid opcode in image\n"); return -1; }
        if ((op==2 && (arg<0 || arg>=string_count)) ||
            ((op==3 || op==4 || op==5) && (arg<0 || arg>=VM_MEM_MAX)) ||
            ((op==23 || op==24) && (arg<0 || arg>code_count))) {
            serial_write("exec: invalid bytecode operand\n"); return -1;
        }
        if (op==25) {
            int id=(arg>>16)&0xffff, argc=arg&0xffff;
            if (id<1 || id>5 || argc>VM_STACK_MAX) {
                serial_write("exec: invalid built-in instruction\n"); return -1;
            }
        }
    }
    for(int i=0;i<VM_MEM_MAX;i++)vm_memory[i]=0;
    vm_sp=0;vm_pc=0;vm_fault=0;u32 budget=VM_INSTRUCTION_BUDGET;int exit_code=0;
    while (vm_pc >= 0 && vm_pc < code_count && !vm_fault) {
        if (budget == 0) {
            serial_write("AEX: instruction limit reached\n");
            vm_fault = 1;
            break;
        }
        budget--;
        VMInstruction in=vm_code[vm_pc++];i64 a,b;
        switch(in.op) {
            case 1: push(in.arg);break;
            case 2: push((i64)(TAG_STRING|(u64)(u32)in.arg));break;
            case 3: push((i64)(TAG_ADDRESS|(u64)(u32)in.arg));break;
            case 4: push(vm_memory[in.arg]);break;
            case 5: a=pop();if(!vm_fault)vm_memory[in.arg]=a;break;
            case 6: a=pop();if(vm_fault)break;if(a<0||a>=VM_MEM_MAX){serial_write("AEX: invalid array read\n");vm_fault=1;}else push(vm_memory[a]);break;
            case 7: b=pop();a=pop();if(vm_fault)break;if(a<0||a>=VM_MEM_MAX){serial_write("AEX: invalid array write\n");vm_fault=1;}else vm_memory[a]=b;break;
            case 8:b=pop();a=pop();if(!vm_fault)push((i64)((u64)a+(u64)b));break;
            case 9:b=pop();a=pop();if(!vm_fault)push((i64)((u64)a-(u64)b));break;
            case 10:b=pop();a=pop();if(!vm_fault)push((i64)((u64)a*(u64)b));break;
            case 11:b=pop();a=pop();if(vm_fault)break;if(!b){serial_write("AEX: division by zero\n");push(0);}else if(a==(-9223372036854775807LL-1)&&b==-1){serial_write("AEX: division overflow\n");vm_fault=1;}else push(a/b);break;
            case 12:b=pop();a=pop();if(vm_fault)break;if(!b){serial_write("AEX: modulo by zero\n");push(0);}else if(a==(-9223372036854775807LL-1)&&b==-1)push(0);else push(a%b);break;
            case 13:b=pop();a=pop();if(!vm_fault)push(a==b);break;
            case 14:b=pop();a=pop();if(!vm_fault)push(a!=b);break;
            case 15:b=pop();a=pop();if(!vm_fault)push(a<b);break;
            case 16:b=pop();a=pop();if(!vm_fault)push(a<=b);break;
            case 17:b=pop();a=pop();if(!vm_fault)push(a>b);break;
            case 18:b=pop();a=pop();if(!vm_fault)push(a>=b);break;
            case 19:b=pop();a=pop();if(!vm_fault)push((a!=0)&&(b!=0));break;
            case 20:b=pop();a=pop();if(!vm_fault)push((a!=0)||(b!=0));break;
            case 21:a=pop();if(!vm_fault)push(!a);break;
            case 22:a=pop();if(!vm_fault)push((i64)(0u-(u64)a));break;
            case 23:vm_pc=in.arg;break;
            case 24:a=pop();if(!vm_fault&&!a)vm_pc=in.arg;break;
            case 25: (void)call_builtin(in.arg);break;
            case 26:(void)pop();break;
            case 27:exit_code=(int)pop();if(!vm_fault)vm_pc=code_count;break;
            default:serial_write("AEX: invalid opcode\n");vm_fault=1;break;
        }
    }
    if(vm_fault || vm_sp<0)return -1;
    return exit_code;
}
int program_run(const char *name) {
    char path[64];
    usize i = 0;
    if (!name || !name[0]) {
        serial_write("exec: missing program name\n");
        return -1;
    }

    while (name[i] && i + 1 < sizeof(path)) {
        path[i] = name[i];
        i++;
    }
    if (name[i]) {
        serial_write("exec: program name is too long\n");
        return -1;
    }
    path[i] = 0;

    int has_extension = i >= 3 && path[i - 3] == '.' &&
                        path[i - 2] == 'a' && path[i - 1] == 'x';
    if (!has_extension) {
        if (i + 3 >= sizeof(path)) {
            serial_write("exec: program name is too long\n");
            return -1;
        }
        path[i++] = '.';
        path[i++] = 'a';
        path[i++] = 'x';
        path[i] = 0;
    }

    const char *image = fs_read(path);
    if (!image) {
        serial_write("exec: executable not found (compile it with cc first)\n");
        return -1;
    }
    return c_execute(image);
}
