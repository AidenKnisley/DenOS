
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <denos/ctype.h>
#include <denos/string.h>
#include <denos/memory.h>
#include <denos/stdlib.h>
#include <denos/math.h>
#include <denos/vector.h>
#include <denos/hashmap.h>
#include <denos/tokenizer.h>
#include <denos/base64.h>
#include <denos/crc32.h>
#include <denos/utf8.h>
#include <denos/path.h>
#include <denos/args.h>
#include <denos/format.h>
#include <denos/ringbuf.h>
#include <denos/bitmap.h>
#include <denos/csv.h>
#include <denos/ini.h>
#include <denos/json.h>
#include <denos/datetime.h>
#include <denos/locale.h>
#include <denos/assert.h>
static int cmpint(const void*a,const void*b){int x=*(const int*)a,y=*(const int*)b;return(x>y)-(x<y);}
int main(void){
 assert(den_isalpha('Q')&&den_isdigit('8')&&den_tolower('A')=='a');
 assert(den_strlen("denos")==5&&den_strcmp("a","b")<0);
 char b[16];assert(den_strlcpy(b,"hello",sizeof b)==5&&!strcmp(b,"hello"));
 char m[8]="abcdef";den_memmove(m+1,m,5);assert(!memcmp(m,"aabcde",6));
 int a[]={4,1,3,2};den_qsort(a,4,sizeof(int),cmpint);assert(a[0]==1&&a[3]==4);
 assert(den_sqrt(81)>8.999&&den_sqrt(81)<9.001&&den_pow_i(2,5)==32);
 den_vec v;int x=42;assert(!den_vec_init(&v,sizeof(int))&&!den_vec_push(&v,&x));assert(*(int*)den_vec_at(&v,0)==42);den_vec_free(&v);
 den_map map;assert(!den_map_init(&map,8));assert(!den_map_put(&map,"answer",(void*)42));assert(den_map_get(&map,"answer")== (void*)42);den_map_free(&map);
 char tok[]="a,b,c",*parts[3];assert(den_split(tok,',',parts,3)==3&&!strcmp(parts[2],"c"));
 char enc[16];assert(den_b64_encode((const unsigned char*)"Man",3,enc,sizeof enc)==4&&!strcmp(enc,"TWFu"));
 assert(den_crc32("123456789",9)==0xcbf43926u);
 const unsigned char u[]={0xe2,0x82,0xac};size_t off=0;unsigned cp=0;assert(!den_utf8_next(u,3,&off,&cp)&&cp==0x20ac);
 assert(den_path_is_safe("/home/a/file")&&!den_path_is_safe("../escape"));
 const char*av[]={"tool","--mode","fast"};assert(den_arg_find(3,av,"--mode")==1&&!strcmp(den_arg_value(3,av,"--mode"),"fast"));
 assert(den_itoa(-123,10,0,b,sizeof b)==4&&!strcmp(b,"-123"));
 den_ringbuf rb;unsigned char ch;assert(!den_rb_init(&rb,2));assert(!den_rb_push(&rb,'x')&&!den_rb_pop(&rb,&ch)&&ch=='x');den_rb_free(&rb);
 unsigned char bits[1]={0};assert(!den_bit_set(bits,8,3,1)&&den_bit_get(bits,8,3)==1);
 const char*csv="\"a,b\",c";char field[16];assert(den_csv_next(&csv,field,sizeof field)==3&&!strcmp(field,"a,b"));
 char val[32];assert(den_ini_get("[net]\nmode = static\n","net","mode",val,sizeof val)==0&&!strcmp(val,"static"));
 const char*js=" \"hello\\nworld\"";assert(den_json_string(&js,val,sizeof val)==11&&!strcmp(val,"hello\nworld"));
 assert(den_is_leap(2000)&&!den_is_leap(1900)&&den_valid_date(2024,2,29));
 assert(!strcmp(den_locale_name(),"C")&&den_assert_value(1)==0);
 puts("DenOS userland library tests passed: 22 modules");
 return 0;
}
