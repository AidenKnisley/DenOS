#include "cvm.h"
#include "serial.h"
#include "fs.h"
#include "types.h"

#define VM_CODE_MAX 768
#define VM_STR_MAX 2048
#define VM_STACK_MAX 2048
#define VM_MEM_MAX 4096
#define VM_GLOBAL_MAX 256
#define VM_CALL_MAX 64
/* AEX images are ASCII hex: magic + count header + 5 bytes/instruction + data. */
#define VM_IMAGE_TEXT_MAX (20u + VM_CODE_MAX * 10u + VM_STR_MAX * 2u)
#ifndef VM_INSTRUCTION_BUDGET
#define VM_INSTRUCTION_BUDGET 50000000u
#endif
#define TAG_STRING ((u64)1<<62)
#define TAG_ADDRESS ((u64)1<<61)
#define TAG_GLOBAL ((u64)1<<60)

typedef struct { u8 op; i32 arg; } VMInstruction;
typedef struct {
    int return_pc;
    int argument_base;
    int argument_count;
    int previous_memory_base;
    int previous_memory_top;
    int entered;
} VMCallFrame;
static VMInstruction vm_code[VM_CODE_MAX];
static char vm_strings[VM_STR_MAX + 1];
static i64 vm_stack[VM_STACK_MAX];
static i64 vm_memory[VM_MEM_MAX];
static i64 vm_globals[VM_GLOBAL_MAX];
static int vm_global_count;
static int vm_sp;
static int vm_pc;
static int vm_fault;
static int vm_string_count;
static int vm_memory_base;
static int vm_memory_top;
static int vm_call_depth;
static int vm_root_entered;
static VMCallFrame vm_calls[VM_CALL_MAX];

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
static int image_text_length(const char *image, usize *length) {
    if (!image || !length) return -1;
    for (usize i = 0; i <= (usize)VM_IMAGE_TEXT_MAX; ++i) {
        if (image[i] == '\0') {
            *length = i;
            return 0;
        }
    }
    return -1;
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
static i64 *resolve_pointer(i64 v) {
    u64 bits = (u64)v;
    if ((bits & TAG_ADDRESS) == 0) return (i64 *)0;
    u64 slot = bits & 0xffffu;
    if (bits & TAG_GLOBAL) {
        return slot < (u64)vm_global_count ? &vm_globals[slot] : (i64 *)0;
    }
    /* Pointers may refer to an active caller frame, but not to a popped frame. */
    return slot < (u64)vm_memory_top && slot < VM_MEM_MAX ? &vm_memory[slot] : (i64 *)0;
}
static int frame_slot(i64 offset) {
    if (offset < 0 || offset >= (i64)(vm_memory_top - vm_memory_base)) return -1;
    int absolute = vm_memory_base + (int)offset;
    return absolute >= 0 && absolute < VM_MEM_MAX ? absolute : -1;
}

static void vm_error(const char *message) {
    serial_write("AEX: ");
    serial_write(message);
    serial_putc('\n');
    vm_fault = 1;
}
static int vm_read_char(i64 source, int offset, u8 *out) {
    if (!out || offset < 0 || offset >= VM_MEM_MAX) return -1;
    u64 bits = (u64)source;
    if (bits & TAG_STRING) {
        u64 at = (bits & 0xffffu) + (u64)offset;
        if (at >= (u64)vm_string_count) return -1;
        *out = (u8)vm_strings[at];
        return 0;
    }
    i64 *cell = resolve_pointer(source + offset);
    if (!cell) return -1;
    *out = (u8)*cell;
    return 0;
}

static int vm_write_char(i64 destination, int offset, u8 value) {
    if (offset < 0 || offset >= VM_MEM_MAX || ((u64)destination & TAG_STRING)) return -1;
    i64 *cell = resolve_pointer(destination + offset);
    if (!cell) return -1;
    *cell = (i64)value;
    return 0;
}

static int vm_string_length(i64 value, int *length) {
    if (!length) return -1;
    for (int i = 0; i < VM_MEM_MAX; ++i) {
        u8 c;
        if (vm_read_char(value, i, &c) < 0) return -1;
        if (!c) { *length = i; return 0; }
    }
    return -1;
}

static int vm_string_compare(i64 left, i64 right, int limit, int *result) {
    if (!result) return -1;
    for (int i = 0; i < limit; ++i) {
        u8 a, b;
        if (vm_read_char(left, i, &a) < 0 || vm_read_char(right, i, &b) < 0) return -1;
        if (a != b) { *result = (int)a - (int)b; return 0; }
        if (!a) { *result = 0; return 0; }
    }
    *result = 0;
    return 0;
}

static void vm_print_string(i64 value) {
    for (int i = 0; i < VM_MEM_MAX; ++i) {
        u8 c;
        if (vm_read_char(value, i, &c) < 0) { vm_error("invalid string pointer"); return; }
        if (!c) return;
        serial_putc((char)c);
    }
    vm_error("unterminated string exceeds VM memory limit");
}

static void print_unsigned_base(u64 value, unsigned base, int uppercase) {
    char digits[32];
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int count = 0;
    if (base < 2 || base > 16) return;
    do { digits[count++] = alphabet[value % base]; value /= base; } while (value && count < (int)sizeof(digits));
    while (count) serial_putc(digits[--count]);
}

static int builtin_printf(int base,int argc) {
    const char *fmt=string_arg(vm_stack[base]);int arg=base+1;
    while(*fmt && !vm_fault) {
        if(*fmt!='%'){serial_putc(*fmt++);continue;}
        fmt++;if(*fmt=='%'){serial_putc('%');fmt++;continue;}
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') fmt++;
        while (*fmt >= '0' && *fmt <= '9') fmt++; /* Width is currently ignored. */
        if (*fmt == '.') { fmt++; while (*fmt >= '0' && *fmt <= '9') fmt++; }
        while (*fmt == 'h' || *fmt == 'l' || *fmt == 'z' || *fmt == 't') fmt++;
        char spec=*fmt?*fmt++:0;
        if(arg>=base+argc)break;
        i64 v=vm_stack[arg++];
        if(spec=='d'||spec=='i')print_i64(v);
        else if(spec=='u')print_unsigned_base((u64)v,10,0);
        else if(spec=='x'||spec=='X')print_unsigned_base((u64)v,16,spec=='X');
        else if(spec=='o')print_unsigned_base((u64)v,8,0);
        else if(spec=='p') { serial_write("0x"); print_unsigned_base((u64)v,16,0); }
        else if(spec=='c')serial_putc((char)v);
        else if(spec=='s')vm_print_string(v);
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
        fmt++;char spec=*fmt?*fmt++:0;i64 *destination=resolve_pointer(vm_stack[ap++]);
        if(!destination)break;
        if(spec=='d'||spec=='i') {
            while(is_ws(input[ip]))ip++;
            int sign=1;if(input[ip]=='-'){sign=-1;ip++;}else if(input[ip]=='+')ip++;
            unsigned base = 10;
            if (spec == 'i' && input[ip] == '0') {
                if (input[ip + 1] == 'x' || input[ip + 1] == 'X') {
                    base = 16;
                    ip += 2;
                } else {
                    base = 8;
                }
            }
            const u64 limit = sign < 0
                ? 0x8000000000000000ULL
                : 0x7FFFFFFFFFFFFFFFULL;
            u64 value = 0;
            int digits = 0;
            int overflow = 0;
            for (;;) {
                int digit = hex_value(input[ip]);
                if (digit < 0 || (unsigned)digit >= base) break;
                ip++;
                digits++;
                if (value > (limit - (u64)digit) / base) {
                    overflow = 1;
                    while ((digit = hex_value(input[ip])) >= 0 &&
                           (unsigned)digit < base) ip++;
                    break;
                }
                value = value * base + (u64)digit;
            }
            if (!digits || overflow) break;
            if (sign < 0) {
                *destination = value == 0x8000000000000000ULL
                    ? (-9223372036854775807LL - 1LL)
                    : -(i64)value;
            } else {
                *destination = (i64)value;
            }
            converted++;
        } else if(spec=='c') {
            if(!input[ip])break;
            *destination=(u8)input[ip++];
            converted++;
        } else break;
    }
    return converted;
}
static i64 builtin_extended(int id, int base, int argc) {
    i64 a = vm_stack[base];
    i64 b = argc > 1 ? vm_stack[base + 1] : 0;
    i64 c = argc > 2 ? vm_stack[base + 2] : 0;
    int n = 0, m = 0, compared = 0;
    u8 ch = 0;
    switch (id) {
        case 6:
            if (vm_string_length(a, &n) < 0) { vm_error("invalid or unterminated string in strlen"); return 0; }
            return n;
        case 7:
            if (vm_string_compare(a, b, VM_MEM_MAX, &compared) < 0) vm_error("invalid string in strcmp");
            return compared;
        case 8:
            if (c < 0) { vm_error("negative length passed to strncmp"); return 0; }
            if (c > VM_MEM_MAX) c = VM_MEM_MAX;
            if (vm_string_compare(a, b, (int)c, &compared) < 0) vm_error("invalid string in strncmp");
            return compared;
        case 9:
        case 10: {
            int limit = id == 9 ? VM_MEM_MAX : (c < 0 ? -1 : (c > VM_MEM_MAX ? VM_MEM_MAX : (int)c));
            if (limit < 0) { vm_error("negative length passed to strncpy"); return 0; }
            for (int i = 0; i < limit; ++i) {
                if (vm_read_char(b, i, &ch) < 0) { vm_error("invalid source pointer in string copy"); return 0; }
                if (vm_write_char(a, i, ch) < 0) { vm_error("invalid destination pointer in string copy"); return 0; }
                if (id == 9 && ch == 0) return a;
                if (id == 10 && ch == 0) {
                    for (++i; i < limit; ++i)
                        if (vm_write_char(a, i, 0) < 0) { vm_error("invalid destination pointer in strncpy"); return 0; }
                    return a;
                }
            }
            if (id == 9) vm_error("source string exceeds VM memory limit in strcpy");
            return a;
        }
        case 11:
            if (vm_string_length(a, &n) < 0) { vm_error("invalid destination string in strcat"); return 0; }
            for (int i = 0; i < VM_MEM_MAX - n; ++i) {
                if (vm_read_char(b, i, &ch) < 0 || vm_write_char(a, n + i, ch) < 0) {
                    vm_error("invalid string pointer in strcat"); return 0;
                }
                if (!ch) return a;
            }
            vm_error("concatenated string exceeds VM memory limit");
            return 0;
        case 12:
        case 13:
            if (b < 0 || b > 255) return 0;
            if (vm_string_length(a, &n) < 0) { vm_error("invalid string in character search"); return 0; }
            if (id == 12) {
                for (int i = 0; i <= n; ++i) {
                    if (vm_read_char(a, i, &ch) < 0) break;
                    if (ch == (u8)b) return a + i;
                }
            } else {
                for (int i = n; i >= 0; --i) {
                    if (vm_read_char(a, i, &ch) < 0) break;
                    if (ch == (u8)b) return a + i;
                }
            }
            return 0;
        case 14: {
            int alen = 0, blen = 0;
            if (vm_string_length(a, &alen) < 0 || vm_string_length(b, &blen) < 0) {
                vm_error("invalid string in strstr"); return 0;
            }
            if (!blen) return a;
            for (int i = 0; i + blen <= alen; ++i) {
                if (vm_string_compare(a + i, b, blen, &compared) < 0) break;
                if (!compared) return a + i;
            }
            return 0;
        }
        case 15:
        case 16:
            if (a == (-9223372036854775807LL - 1LL)) { vm_error("absolute value overflow"); return 0; }
            return a < 0 ? -a : a;
        case 17: {
            int i = 0, sign = 1;
            while (vm_read_char(a, i, &ch) == 0 && (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) i++;
            if (vm_read_char(a, i, &ch) < 0) { vm_error("invalid string passed to atoi"); return 0; }
            if (ch == '+' || ch == '-') { if (ch == '-') sign = -1; i++; }
            u64 value = 0; int digits = 0;
            const u64 limit = sign < 0 ? 0x8000000000000000ULL : 0x7fffffffffffffffULL;
            while (vm_read_char(a, i, &ch) == 0 && ch >= '0' && ch <= '9') {
                unsigned digit = ch - '0';
                if (value > (limit - digit) / 10u) { vm_error("atoi integer overflow"); return 0; }
                value = value * 10u + digit; ++i; ++digits;
            }
            if (!digits) return 0;
            if (sign < 0) return value == 0x8000000000000000ULL ? (-9223372036854775807LL - 1LL) : -(i64)value;
            return (i64)value;
        }
        case 18: return a >= '0' && a <= '9';
        case 19: return (a >= 'A' && a <= 'Z') || (a >= 'a' && a <= 'z');
        case 20: return a == ' ' || a == '\t' || a == '\n' || a == '\r' || a == '\v' || a == '\f';
        case 21: return a >= 'a' && a <= 'z' ? a - ('a' - 'A') : a;
        case 22: return a >= 'A' && a <= 'Z' ? a + ('a' - 'A') : a;
        case 23: return (a >= '0' && a <= '9') || (a >= 'A' && a <= 'Z') || (a >= 'a' && a <= 'z');
        case 24: return a >= 'A' && a <= 'Z';
        case 25: return a >= 'a' && a <= 'z';
        case 26: return (a >= '0' && a <= '9') || (a >= 'A' && a <= 'F') || (a >= 'a' && a <= 'f');
        case 27: return a < 32 || a == 127;
        case 28: return a >= 32 && a <= 126;
        case 29: return a >= 33 && a <= 126;
        case 30: return (a >= 33 && a <= 126) &&
                        !((a >= '0' && a <= '9') || (a >= 'A' && a <= 'Z') || (a >= 'a' && a <= 'z'));
        default: (void)m; return 0;
    }
}

static i64 call_builtin(int encoded) {
    int id=(encoded>>16)&0xffff,argc=encoded&0xffff,base=vm_sp-argc;
    if (vm_fault) return 0;
    if(argc<0 || argc>VM_STACK_MAX || base<0 || id<1 || id>30){
        serial_write("AEX: invalid built-in call\n"); vm_fault=1; return 0;
    }
    if ((id==2 || id==3 || id==6 || id==15 || id==16 || id==17 || (id>=18 && id<=30)) && argc!=1) { vm_error("wrong built-in argument count"); return 0; }
    if (id==4 && argc<2) { vm_error("wrong scanf argument count"); return 0; }
    if (id==5 && argc!=0) { vm_error("wrong getchar argument count"); return 0; }
    if (id==1 && argc<1) { vm_error("wrong printf argument count"); return 0; }
    if ((id==7 || id==9 || id==11 || id==12 || id==13 || id==14) && argc!=2) { vm_error("wrong string built-in argument count"); return 0; }
    if ((id==8 || id==10) && argc!=3) { vm_error("wrong string built-in argument count"); return 0; }
    i64 result=0;
    if(id==1)result=builtin_printf(base,argc);
    else if(id==2){vm_print_string(vm_stack[base]);if(!vm_fault)serial_putc('\n');}
    else if(id==3)serial_putc((char)vm_stack[base]);
    else if(id==4)result=builtin_scanf(base,argc);
    else if(id==5)result=(u8)serial_getc();
    else result=builtin_extended(id,base,argc);
    if (!vm_fault) { vm_sp=base; push(result); }
    return result;
}
int c_execute(const char *image) {
    if(!image || image[0]!='A' || image[1]!='E' || image[2]!='X' ||
       (image[3]!='1' && image[3]!='2' && image[3]!='3' && image[3]!='4' && image[3]!='5' && image[3]!='6')) {
        serial_write("exec: not an Aiden executable\n");
        return -1;
    }
    int image_version = image[3] - '0';
    const char *p=image+4;u8 h[4];
    for(int i=0;i<4;i++)if(read_hex_byte(&p,&h[i])<0){serial_write("exec: corrupt header\n");return -1;}
    int code_count=((int)h[0]<<8)|h[1],string_count=((int)h[2]<<8)|h[3];
    int global_count = 0;
    if (image_version >= 6) {
        u8 ext[4];
        for (int i = 0; i < 4; ++i)
            if (read_hex_byte(&p, &ext[i]) < 0) { serial_write("exec: corrupt AEX6 header\n"); return -1; }
        global_count = ((int)ext[0] << 8) | ext[1];
        if (ext[2] || ext[3]) { serial_write("exec: unsupported AEX6 flags\n"); return -1; }
    }
    if(code_count<1||code_count>VM_CODE_MAX||string_count<0||string_count>VM_STR_MAX||
       global_count<0||global_count>VM_GLOBAL_MAX||
       (usize)global_count*8u>(usize)string_count) { serial_write("exec: invalid executable size\n");return -1;}
    usize actual_length = 0;
    usize expected_length = (image_version >= 6 ? 20u : 12u) +
                            (usize)code_count * 10u + (usize)string_count * 2u;
    if (image_text_length(image, &actual_length) < 0 || actual_length != expected_length) {
        serial_write("exec: executable length does not match its header\n");
        return -1;
    }
    for(int i=0;i<code_count;i++) {
        u8 b[5];for(int j=0;j<5;j++)if(read_hex_byte(&p,&b[j])<0){serial_write("exec: truncated bytecode\n");return -1;}
        vm_code[i].op=b[0];vm_code[i].arg=(i32)((u32)b[1]|((u32)b[2]<<8)|((u32)b[3]<<16)|((u32)b[4]<<24));
    }
    for(int i=0;i<string_count;i++){u8 b;if(read_hex_byte(&p,&b)<0){serial_write("exec: truncated strings\n");return -1;}vm_strings[i]=(char)b;}
    vm_strings[string_count]=0; /* Bound string reads for malformed images. */
    int payload_count = string_count - global_count * 8;
    vm_string_count = payload_count;
    vm_global_count = global_count;
    for (int i = 0; i < VM_GLOBAL_MAX; ++i) vm_globals[i] = 0;
    if (global_count) {
        int init_at = payload_count;
        for (int i = 0; i < global_count; ++i) {
            u64 value = 0;
            for (int byte = 0; byte < 8; ++byte)
                value |= (u64)(u8)vm_strings[init_at + i * 8 + byte] << (byte * 8);
            vm_globals[i] = (i64)value;
        }
    }

    /* Reject malformed bytecode before interpreting any of it. */
    for (int i=0;i<code_count;i++) {
        int op=vm_code[i].op, arg=vm_code[i].arg;
        int max_opcode = image_version == 1 ? 27 :
                         (image_version == 2 ? 35 : (image_version == 3 ? 37 :
                         (image_version == 4 ? 38 : (image_version == 5 ? 40 : 43))));
        if (op<1 || op>max_opcode) { serial_write("exec: invalid opcode for executable version\n"); return -1; }
        if ((op==2 && (arg<0 || arg>=payload_count)) ||
            ((op==3 || op==4 || op==5) && (arg<0 || arg>=VM_MEM_MAX)) ||
            ((op==41 || op==42 || op==43) && (image_version < 6 || arg < 0 || arg >= global_count)) ||
            ((op==23 || op==24) && (arg<0 || arg>code_count)) ||
            (op==35 && (arg<0 || arg>payload_count-8))) {
            serial_write("exec: invalid bytecode operand\n"); return -1;
        }
        if (op==25) {
            int id=(arg>>16)&0xffff, argc=arg&0xffff;
            if (id<1 || id>30 || argc>VM_STACK_MAX) {
                serial_write("exec: invalid built-in instruction\n"); return -1;
            }
        }
        if (op==36) {
            u32 encoded=(u32)arg;
            int target=(int)(encoded&0xffffu);
            int argc=(int)((encoded>>16)&0xffu);
            if (image_version < 3 || target<0 || target>=code_count ||
                argc>VM_CALL_MAX || vm_code[target].op!=37) {
                serial_write("exec: invalid user-function call\n"); return -1;
            }
        }
        if (op==37 && (image_version < 3 || arg<0 || arg>VM_MEM_MAX)) {
            serial_write("exec: invalid function frame size\n"); return -1;
        }
        if (op==38 && (image_version < 4 || arg<1 || arg>4096)) {
            serial_write("exec: invalid array bound\n"); return -1;
        }
        if ((op==39 || op==40) && image_version < 5) {
            serial_write("exec: pointer opcode requires AEX5\n"); return -1;
        }
        if ((op==41 || op==42 || op==43) && image_version < 6) {
            serial_write("exec: global storage opcode requires AEX6\n"); return -1;
        }
    }
    for(int i=0;i<VM_MEM_MAX;i++)vm_memory[i]=0;
    vm_sp=0;vm_pc=0;vm_fault=0;
    vm_memory_base=0;vm_memory_top=(image_version < 3 ? VM_MEM_MAX : 0);vm_call_depth=0;vm_root_entered=0;
    u32 budget=VM_INSTRUCTION_BUDGET;int exit_code=0;
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
            case 3: {
                int slot=frame_slot(in.arg);
                if(slot<0)vm_error("invalid address of local variable");
                else push((i64)(TAG_ADDRESS|(u64)(u32)slot));
                break;
            }
            case 4: {
                int slot=frame_slot(in.arg);
                if(slot<0)vm_error("invalid local variable read");
                else push(vm_memory[slot]);
                break;
            }
            case 5: {
                a=pop();
                int slot=frame_slot(in.arg);
                if(!vm_fault && slot<0)vm_error("invalid local variable write");
                else if(!vm_fault)vm_memory[slot]=a;
                break;
            }
            case 6: {
                a=pop();if(vm_fault)break;
                if(a<0||a>=(i64)(vm_memory_top-vm_memory_base))vm_error("array read outside the current function frame");
                else push(vm_memory[vm_memory_base+(int)a]);
                break;
            }
            case 7: {
                b=pop();a=pop();if(vm_fault)break;
                if(a<0||a>=(i64)(vm_memory_top-vm_memory_base))vm_error("array write outside the current function frame");
                else vm_memory[vm_memory_base+(int)a]=b;
                break;
            }
            case 8:b=pop();a=pop();if(!vm_fault)push((i64)((u64)a+(u64)b));break;
            case 9:b=pop();a=pop();if(!vm_fault)push((i64)((u64)a-(u64)b));break;
            case 10:b=pop();a=pop();if(!vm_fault)push((i64)((u64)a*(u64)b));break;
            case 11:
                b=pop();a=pop();if(vm_fault)break;
                if(!b)vm_error("division by zero");
                else if(a==(-9223372036854775807LL-1)&&b==-1)vm_error("division overflow");
                else push(a/b);
                break;
            case 12:
                b=pop();a=pop();if(vm_fault)break;
                if(!b)vm_error("modulo by zero");
                else if(a==(-9223372036854775807LL-1)&&b==-1)
                    vm_error("modulo overflow");
                else push(a%b);
                break;
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
            case 27: {
                a=pop();
                if(vm_fault)break;
                if(vm_call_depth>0) {
                    VMCallFrame frame=vm_calls[vm_call_depth-1];
                    if(!frame.entered) { vm_error("return before function frame setup"); break; }
                    vm_call_depth--;
                    vm_sp=frame.argument_base;
                    vm_memory_base=frame.previous_memory_base;
                    vm_memory_top=frame.previous_memory_top;
                    vm_pc=frame.return_pc;
                    push(a);
                } else {
                    exit_code=(int)a;
                    vm_pc=code_count;
                }
                break;
            }
            case 28:b=pop();a=pop();if(!vm_fault)push((i64)((u64)a&(u64)b));break;
            case 29:b=pop();a=pop();if(!vm_fault)push((i64)((u64)a|(u64)b));break;
            case 30:b=pop();a=pop();if(!vm_fault)push((i64)((u64)a^(u64)b));break;
            case 31:
                b=pop();a=pop();
                if(!vm_fault) {
                    if(b<0 || b>=64) { serial_write("AEX: invalid shift count\n"); vm_fault=1; }
                    else push((i64)((u64)a << (unsigned)b));
                }
                break;
            case 32:
                b=pop();a=pop();
                if(!vm_fault) {
                    if(b<0 || b>=64) { serial_write("AEX: invalid shift count\n"); vm_fault=1; }
                    else if(b==0) push(a);
                    else if(a>=0) push((i64)((u64)a >> (unsigned)b));
                    else push((i64)(((u64)a >> (unsigned)b) | (~(u64)0 << (64u-(unsigned)b))));
                }
                break;
            case 33:a=pop();if(!vm_fault)push((i64)~(u64)a);break;
            case 34:
                if(vm_sp<=0) { serial_write("AEX: stack underflow\n"); vm_fault=1; }
                else push(vm_stack[vm_sp-1]);
                break;
            case 35: {
                u64 bits=0;
                for(int j=0;j<8;j++) bits|=((u64)(u8)vm_strings[in.arg+j])<<(j*8);
                push((i64)bits);
                break;
            }
            case 36: {
                u32 encoded=(u32)in.arg;
                int target=(int)(encoded&0xffffu);
                int argc=(int)((encoded>>16)&0xffu);
                int argument_base=vm_sp-argc;
                if(vm_call_depth>=VM_CALL_MAX) {
                    vm_error("function call stack limit reached");
                } else if(argc<0 || argument_base<0 || target<0 || target>=code_count ||
                          vm_code[target].op!=37) {
                    vm_error("invalid user-function call frame");
                } else {
                    VMCallFrame *frame=&vm_calls[vm_call_depth++];
                    frame->return_pc=vm_pc;
                    frame->argument_base=argument_base;
                    frame->argument_count=argc;
                    frame->previous_memory_base=vm_memory_base;
                    frame->previous_memory_top=vm_memory_top;
                    frame->entered=0;
                    vm_pc=target;
                }
                break;
            }
            case 37: {
                int local_count=in.arg;
                int base=vm_memory_top;
                if(local_count<0 || local_count>VM_MEM_MAX || base<0 ||
                   local_count>VM_MEM_MAX-base) {
                    vm_error("function local storage limit exceeded");
                    break;
                }
                if(vm_call_depth>0) {
                    VMCallFrame *frame=&vm_calls[vm_call_depth-1];
                    if(frame->entered || frame->argument_count>local_count) {
                        vm_error("function parameters do not fit its local frame");
                        break;
                    }
                    for(int i=0;i<local_count;i++)vm_memory[base+i]=0;
                    for(int i=0;i<frame->argument_count;i++) {
                        vm_memory[base+i]=vm_stack[frame->argument_base+i];
                    }
                    vm_sp=frame->argument_base;
                    frame->entered=1;
                } else {
                    if(vm_root_entered) {
                        vm_error("multiple root function frames");
                        break;
                    }
                    for(int i=0;i<local_count;i++)vm_memory[base+i]=0;
                    vm_root_entered=1;
                }
                vm_memory_base=base;
                vm_memory_top=base+local_count;
                break;
            }
            case 38:
                if (vm_sp <= 0) vm_error("array bounds check stack underflow");
                else if (vm_stack[vm_sp - 1] < 0 || vm_stack[vm_sp - 1] >= in.arg)
                    vm_error("array index out of bounds");
                break;
            case 39: {
                a = pop();
                if (vm_fault) break;
                i64 *cell = resolve_pointer(a);
                if (!cell) vm_error("invalid pointer dereference");
                else push(*cell);
                break;
            }
            case 40: {
                b = pop();
                a = pop();
                if (vm_fault) break;
                i64 *cell = resolve_pointer(a);
                if (!cell) vm_error("invalid pointer store");
                else *cell = b;
                break;
            }
            case 41:
                if (in.arg < 0 || in.arg >= vm_global_count) vm_error("invalid global read");
                else push(vm_globals[in.arg]);
                break;
            case 42:
                a = pop();
                if (!vm_fault) {
                    if (in.arg < 0 || in.arg >= vm_global_count) vm_error("invalid global write");
                    else vm_globals[in.arg] = a;
                }
                break;
            case 43:
                if (in.arg < 0 || in.arg >= vm_global_count) vm_error("invalid global address");
                else push((i64)(TAG_ADDRESS | TAG_GLOBAL | (u64)(u32)in.arg));
                break;
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
