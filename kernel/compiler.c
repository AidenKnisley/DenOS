#include "cvm.h"
#include "types.h"

/*
 * Aiden C is a deliberately small freestanding C subset compiler.
 * It compiles one int main(void) function to AEX1 stack bytecode.
 */

#define MAX_CODE 768
#define MAX_STRINGS 2048
#define MAX_SYMBOLS 160
#define TOK_TEXT 128

enum { T_EOF=0, T_ID=256, T_NUM, T_STRING, T_CHAR };
enum {
    BC_PUSHI=1, BC_PUSHS, BC_PUSHADDR, BC_LOAD, BC_STORE,
    BC_ALOAD, BC_ASTORE, BC_ADD, BC_SUB, BC_MUL, BC_DIV, BC_MOD,
    BC_EQ, BC_NE, BC_LT, BC_LE, BC_GT, BC_GE, BC_AND, BC_OR,
    BC_NOT, BC_NEG, BC_JMP, BC_JZ, BC_CALL, BC_POP, BC_RET
};

typedef struct { u8 op; i32 arg; } Instruction;
typedef struct { int kind; char text[TOK_TEXT]; i64 value; int line; } Token;
typedef struct { char name[32]; int base; int count; } Symbol;

static Instruction code[MAX_CODE];
static int code_count;
static char strings[MAX_STRINGS];
static int strings_count;
static Symbol symbols[MAX_SYMBOLS];
static int symbol_count;
static const char *source_ptr;
static Token tok;
static int source_line;
static int failed;
static char error_text[96];

static int streq(const char *a, const char *b) {
    int i=0; while(a[i] && b[i] && a[i]==b[i]) i++; return a[i]==b[i];
}
static int is_space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
static int is_alpha(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static int is_digit(char c) { return c>='0'&&c<='9'; }
static void set_error(const char *s) {
    if (failed) return;
    failed=1; int i=0; while(s[i] && i<(int)sizeof(error_text)-1){error_text[i]=s[i];i++;} error_text[i]=0;
}
static char take_char(void) {
    char c=*source_ptr;
    if (c) source_ptr++;
    if (c=='\n') source_line++;
    return c;
}
static void skip_space(void) {
    for (;;) {
        while (is_space(*source_ptr)) take_char();
        if (*source_ptr=='#') { while(*source_ptr && take_char()!='\n'){} continue; }
        if (source_ptr[0]=='/' && source_ptr[1]=='/') { while(*source_ptr && take_char()!='\n'){} continue; }
        if (source_ptr[0]=='/' && source_ptr[1]=='*') {
            take_char(); take_char();
            while(*source_ptr && !(source_ptr[0]=='*'&&source_ptr[1]=='/')) take_char();
            if (*source_ptr) {
                take_char(); take_char();
            } else {
                set_error("unterminated block comment");
                return;
            }
            continue;
        }
        break;
    }
}
static char escaped_char(void) {
    char c=take_char();
    if(c!='\\') return c;
    c=take_char();
    if(c=='n') return '\n';
    if(c=='r') return '\r';
    if(c=='t') return '\t';
    if(c=='0') return 0;
    if(c=='\\') return '\\';
    if(c=='\'') return '\'';
    if(c=='"') return '"';
    if(c=='e') return 27;
    return c;
}
static void next_token(void) {
    skip_space(); tok.text[0]=0; tok.value=0; tok.line=source_line;
    char c=*source_ptr;
    if(!c){tok.kind=T_EOF;return;}
    if(is_alpha(c)) {
        int n=0;
        while(is_alpha(*source_ptr)||is_digit(*source_ptr)) {
            if(n<TOK_TEXT-1) tok.text[n++]=take_char();
            else { take_char(); set_error("identifier is too long"); }
        }
        tok.text[n]=0;
        if (n > 31) set_error("identifier is too long (maximum 31 characters)");
        tok.kind=T_ID;
        return;
    }
    if(is_digit(c)) {
        i64 v=0;
        while(is_digit(*source_ptr)) {
            int digit = take_char() - '0';
            if (v > (0x7FFFFFFFLL - digit) / 10) {
                set_error("integer literal exceeds the signed 32-bit immediate range");
                while (is_digit(*source_ptr)) take_char();
                tok.kind=T_NUM;
                tok.value=0;
                return;
            }
            v=v*10+digit;
        }
        tok.kind=T_NUM; tok.value=v; return;
    }
    if(c=='"') {
        take_char(); int n=0;
        while(*source_ptr && *source_ptr!='"') { char q=escaped_char(); if(n<TOK_TEXT-1)tok.text[n++]=q; else set_error("string literal too long"); }
        if(*source_ptr=='"')take_char(); else set_error("unterminated string literal");
        tok.text[n]=0; tok.kind=T_STRING; return;
    }
    if(c=='\'') {
        take_char(); tok.value=(u8)escaped_char();
        if(*source_ptr=='\'')take_char(); else set_error("unterminated character literal");
        tok.kind=T_CHAR; return;
    }
    tok.kind=(u8)take_char(); tok.text[0]=(char)tok.kind; tok.text[1]=0;
    if((tok.kind=='='&&*source_ptr=='=')||(tok.kind=='!'&&*source_ptr=='=')||
       (tok.kind=='<'&&*source_ptr=='=')||(tok.kind=='>'&&*source_ptr=='=')||
       (tok.kind=='&'&&*source_ptr=='&')||(tok.kind=='|'&&*source_ptr=='|')||
       (tok.kind=='+'&&*source_ptr=='+')||(tok.kind=='-'&&*source_ptr=='-')) {
        tok.text[1]=take_char(); tok.text[2]=0;
    }
}
static int accept(const char *s) { if(streq(tok.text,s)){next_token();return 1;} return 0; }
static void expect(const char *s) { if(!accept(s)) set_error("unexpected token (expected punctuation/keyword)"); }
static int emit(int op, i32 arg) {
    if(code_count>=MAX_CODE){set_error("program too large for Aiden C bytecode");return 0;}
    code[code_count].op=(u8)op; code[code_count].arg=arg; return code_count++;
}
static void patch(int at, int target) { if(at>=0&&at<code_count)code[at].arg=target; }
static int add_string(const char *s) {
    int start=strings_count, i=0;
    while(s[i]) { if(strings_count>=MAX_STRINGS-1){set_error("string table full");return 0;} strings[strings_count++]=s[i++]; }
    if(strings_count>=MAX_STRINGS){set_error("string table full");return 0;}
    strings[strings_count++]=0; return start;
}
static int find_symbol(const char *name) {
    for(int i=0;i<symbol_count;i++)if(streq(symbols[i].name,name))return i;
    return -1;
}
static int add_symbol(const char *name, int count) {
    if(symbol_count>=MAX_SYMBOLS){set_error("too many variables");return -1;}
    if(find_symbol(name)>=0){set_error("duplicate variable name (use unique names)");return -1;}
    int n=0;
    while(name[n] && n<31){symbols[symbol_count].name[n]=name[n];n++;}
    if (name[n]) { set_error("identifier is too long (maximum 31 characters)"); return -1; }
    symbols[symbol_count].name[n]=0;
    int base=0;for(int i=0;i<symbol_count;i++)base+=symbols[i].count;
    if(count<1||count>4096-base){set_error("too much variable storage");return -1;}
    symbols[symbol_count].base=base;symbols[symbol_count].count=count;return symbol_count++;
}
static int require_symbol(const char *name) {
    int s=find_symbol(name); if(s<0)set_error("unknown variable"); return s;
}
static int parse_expression(void);
static void parse_statement(void);
static int builtin_id(const char *name) {
    if(streq(name,"printf"))return 1;
    if(streq(name,"puts"))return 2;
    if(streq(name,"putchar"))return 3;
    if(streq(name,"scanf"))return 4;
    if(streq(name,"getchar"))return 5;
    return -1;
}
static void validate_call(int id,int argc) {
    if(id<0 || (id==2&&argc!=1) || (id==3&&argc!=1) ||
       (id==5&&argc!=0) || (id==1&&argc<1) || (id==4&&argc<2))
        set_error("unknown built-in or wrong number of function arguments");
}
static int parse_call(const char *name) {
    int id=builtin_id(name), argc=0;
    if(id<0)set_error("only built-in functions are supported in this Aiden C version");
    expect("(");
    if(!accept(")")) {
        do { parse_expression(); argc++; } while(accept(","));
        expect(")");
    }
    validate_call(id,argc);
    emit(BC_CALL,(id<<16)|(argc&0xffff));
    return 1;
}
static void parse_primary(void) {
    if(tok.kind==T_NUM||tok.kind==T_CHAR){emit(BC_PUSHI,(i32)tok.value);next_token();return;}
    if(tok.kind==T_STRING){int off=add_string(tok.text);emit(BC_PUSHS,off);next_token();return;}
    if(accept("(")){parse_expression();expect(")");return;}
    if(accept("&")) {
        if(tok.kind!=T_ID){set_error("address operator needs a variable");return;}
        char name[32];int j=0;while(tok.text[j]&&j<31){name[j]=tok.text[j];j++;}name[j]=0;next_token();
        int si=require_symbol(name); if(si>=0){if(symbols[si].count!=1)set_error("only scalar variable addresses are supported");emit(BC_PUSHADDR,symbols[si].base);} return;
    }
    if(accept("!")){parse_primary();emit(BC_NOT,0);return;}
    if(accept("-")){parse_primary();emit(BC_NEG,0);return;}
    if(accept("+")){parse_primary();return;}
    if(tok.kind==T_ID) {
        char name[32];int j=0;while(tok.text[j]&&j<31){name[j]=tok.text[j];j++;}name[j]=0;next_token();
        if(streq(tok.text,"(")){parse_call(name);return;}
        int si=require_symbol(name); if(si<0)return;
        if(accept("[")){if(symbols[si].count==1)set_error("subscripted value is not an array");emit(BC_PUSHI,symbols[si].base);parse_expression();expect("]");emit(BC_ADD,0);emit(BC_ALOAD,0);}
        else {if(symbols[si].count!=1)set_error("array needs an index");emit(BC_LOAD,symbols[si].base);}
        return;
    }
    set_error("expected expression"); if(tok.kind!=T_EOF)next_token();
}
static void parse_mul(void){parse_primary();while(streq(tok.text,"*")||streq(tok.text,"/")||streq(tok.text,"%")){char op=tok.text[0];next_token();parse_primary();emit(op=='*'?BC_MUL:(op=='/'?BC_DIV:BC_MOD),0);}}
static void parse_add(void){parse_mul();while(streq(tok.text,"+")||streq(tok.text,"-")){char op=tok.text[0];next_token();parse_mul();emit(op=='+'?BC_ADD:BC_SUB,0);}}
static void parse_rel(void){parse_add();while(streq(tok.text,"<")||streq(tok.text,">")||streq(tok.text,"<=")||streq(tok.text,">=")){char op[3];int k=0;while(tok.text[k]&&k<2){op[k]=tok.text[k];k++;}op[k]=0;next_token();parse_add();emit(streq(op,"<")?BC_LT:streq(op,">")?BC_GT:streq(op,"<=")?BC_LE:BC_GE,0);}}
static void parse_eq(void){parse_rel();while(streq(tok.text,"==")||streq(tok.text,"!=")){int ne=streq(tok.text,"!=");next_token();parse_rel();emit(ne?BC_NE:BC_EQ,0);}}
static void parse_and(void) {
    parse_eq();
    while(accept("&&")) {
        int false_jump=emit(BC_JZ,0);
        parse_eq();
        emit(BC_NOT,0); emit(BC_NOT,0);
        int end_jump=emit(BC_JMP,0);
        patch(false_jump,code_count);
        emit(BC_PUSHI,0);
        patch(end_jump,code_count);
    }
}
static void parse_or(void) {
    parse_and();
    while(accept("||")) {
        int evaluate_right=emit(BC_JZ,0);
        emit(BC_PUSHI,1);
        int end_jump=emit(BC_JMP,0);
        patch(evaluate_right,code_count);
        parse_and();
        emit(BC_NOT,0); emit(BC_NOT,0);
        patch(end_jump,code_count);
    }
}
static int parse_expression(void){parse_or();return 1;}
static int is_type(void){return streq(tok.text,"int")||streq(tok.text,"char")||streq(tok.text,"long")||streq(tok.text,"unsigned")||streq(tok.text,"signed");}
static void parse_declaration(int with_semicolon) {
    if(!is_type()){set_error("expected variable type");return;}
    next_token(); if(tok.kind==T_ID && (streq(tok.text,"int")||streq(tok.text,"char")))next_token();
    if(tok.kind!=T_ID){set_error("expected variable name");return;}
    char name[32];int j=0;while(tok.text[j]&&j<31){name[j]=tok.text[j];j++;}name[j]=0;next_token();
    int count=1;
    if(accept("[")){
        if(tok.kind!=T_NUM){set_error("array length must be a constant integer");return;}
        if(tok.value<1 || tok.value>4096){set_error("array length must be between 1 and 4096");return;}
        count=(int)tok.value;next_token();expect("]");
    }
    int si=add_symbol(name,count); if(si<0)return;
    if(accept("=")) {
        if(count!=1){set_error("array initializers are not supported; assign elements individually");return;}
        parse_expression();emit(BC_STORE,symbols[si].base);
    }
    if(with_semicolon)expect(";");
}
static void parse_simple_statement(int semicolon) {
    if(tok.kind==T_ID) {
        char name[32];int j=0;while(tok.text[j]&&j<31){name[j]=tok.text[j];j++;}name[j]=0;next_token();
        if(accept("(")) {
            int id=builtin_id(name),argc=0;
            if(id<0)set_error("unknown built-in function");
            if(!accept(")")){do{parse_expression();argc++;}while(accept(","));expect(")");}
            validate_call(id,argc);
            emit(BC_CALL,(id<<16)|(argc&0xffff));emit(BC_POP,0);
            if(semicolon)expect(";");
            return;
        }
        int si=require_symbol(name); if(si<0)return;
        if(accept("[")) {
            if(symbols[si].count==1)set_error("subscripted value is not an array");
            emit(BC_PUSHI,symbols[si].base);parse_expression();expect("]");emit(BC_ADD,0);
            if(accept("=")){parse_expression();emit(BC_ASTORE,0);}else set_error("array statement must assign an element");
        } else if(streq(tok.text,"++")||streq(tok.text,"--")) {
            int dec=streq(tok.text,"--");next_token();
            emit(BC_LOAD,symbols[si].base);emit(BC_PUSHI,1);emit(dec?BC_SUB:BC_ADD,0);emit(BC_STORE,symbols[si].base);
        } else if(accept("=")) {parse_expression();emit(BC_STORE,symbols[si].base);}
        else set_error("expected assignment, increment, or function call");
        if(semicolon)expect(";");
        return;
    }
    if(semicolon)expect(";");
}
static void parse_block(void) {
    expect("{");
    while(tok.kind!=T_EOF&&!streq(tok.text,"}")&&!failed){if(is_type())parse_declaration(1);else parse_statement();}
    expect("}");
}
static void parse_statement(void) {
    if(failed)return;
    if(streq(tok.text,"{")){parse_block();return;}
    if(accept(";"))return;
    if(accept("if")) {
        expect("(");parse_expression();expect(")");int jf=emit(BC_JZ,0);parse_statement();
        if(accept("else")){int j=emit(BC_JMP,0);patch(jf,code_count);parse_statement();patch(j,code_count);}else patch(jf,code_count);
        return;
    }
    if(accept("while")) {
        int start=code_count;expect("(");parse_expression();expect(")");int jf=emit(BC_JZ,0);parse_statement();emit(BC_JMP,start);patch(jf,code_count);return;
    }
    if(accept("for")) {
        expect("(");if(is_type())parse_declaration(1);else parse_simple_statement(1);
        int cond=code_count;
        if(!streq(tok.text,";"))parse_expression();else emit(BC_PUSHI,1);
        expect(";");int jf=emit(BC_JZ,0);int jump_body=emit(BC_JMP,0);int inc=code_count;
        if(!streq(tok.text,")")) parse_simple_statement(0);
        expect(")");
        emit(BC_JMP,cond);
        patch(jump_body,code_count);parse_statement();emit(BC_JMP,inc);patch(jf,code_count);return;
    }
    if(accept("return")){if(!streq(tok.text,";"))parse_expression();else emit(BC_PUSHI,0);expect(";");emit(BC_RET,0);return;}
    parse_simple_statement(1);
}
static void write_hex(char *out, usize cap, usize *pos, u8 b) {
    static const char hex[]="0123456789ABCDEF";
    if(*pos+2>=cap){failed=1;return;}
    out[(*pos)++]=hex[(b>>4)&15];out[(*pos)++]=hex[b&15];
}
int c_compile(const char *src, char *image, usize cap) {
    code_count=0;strings_count=0;symbol_count=0;source_ptr=src;source_line=1;failed=0;error_text[0]=0;
    if(!src||!image||cap<32){set_error("invalid compiler input");return -1;}
    next_token();
    while(tok.kind!=T_EOF&&!failed) {
        if(is_type()) {
            next_token();
            if(tok.kind!=T_ID){set_error("expected function name");break;}
            char fn[32];int n=0;while(tok.text[n]&&n<31){fn[n]=tok.text[n];n++;}fn[n]=0;next_token();
            if(!streq(fn,"main")){set_error("only the main function is supported");break;}
            expect("(");if(!accept(")")){if(accept("void"))expect(")");else set_error("main must have no parameters");}
            parse_block();
            if(!failed){emit(BC_PUSHI,0);emit(BC_RET,0);}
            break;
        } else {set_error("expected main function");break;}
    }
    if(!failed&&tok.kind!=T_EOF)set_error("extra content after main function");
    if(failed)return -1;
    usize p=0;
    if(cap<9){set_error("executable output buffer too small");return -1;}
    image[p++]='A';image[p++]='E';image[p++]='X';image[p++]='1';
    static const char hex[]="0123456789ABCDEF";
    u16 cc=(u16)code_count, ss=(u16)strings_count;
    u8 header[4]={(u8)(cc>>8),(u8)cc,(u8)(ss>>8),(u8)ss};
    for(int i=0;i<4;i++){if(p+2>=cap){set_error("executable output too large");return -1;}image[p++]=hex[header[i]>>4];image[p++]=hex[header[i]&15];}
    for(int i=0;i<code_count;i++){
        u32 a=(u32)code[i].arg;
        write_hex(image,cap,&p,code[i].op);
        write_hex(image,cap,&p,(u8)(a&255));write_hex(image,cap,&p,(u8)((a>>8)&255));
        write_hex(image,cap,&p,(u8)((a>>16)&255));write_hex(image,cap,&p,(u8)((a>>24)&255));
    }
    for(int i=0;i<strings_count;i++)write_hex(image,cap,&p,(u8)strings[i]);
    if(failed)return -1;
    image[p]=0;
    return (int)p;
}
const char *c_compile_error(void){return error_text[0]?error_text:"compile failed";}
