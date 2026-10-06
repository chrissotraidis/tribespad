#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tbimp_CompareStringEx(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){if(pc==1)return (void *)tbimp_CompareStringEx;if(pc==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
struct Case {uint32_t flags;const uint16_t *a;int32_t na;const uint16_t *b;int32_t nb;uint32_t result;};
#include "fixtures/collation_cases.inc"
static unsigned char *memory;
static struct GuestCase {uint32_t flags,a,na,b,nb,result;} cases[100];
static unsigned case_count;static uint32_t cursor=0x200001,locale;
static uint32_t save(const uint16_t *p,uint32_t count){uint32_t at=cursor;assert(cursor+2*count<0x210000);memory[at-1]=0x79;memcpy(memory+at,p,2*count);memory[at+2*count]=0x79;cursor+=2*count+3;return at;}
static void add(uint32_t flags,const uint16_t *a,int32_t na,const uint16_t *b,int32_t nb,uint32_t expected){unsigned ac=0,bc=0;do{ac++;}while(a[ac-1]);do{bc++;}while(b[bc-1]);if(na>=0&&ac<(uint32_t)na)ac=(uint32_t)na;if(nb>=0&&bc<(uint32_t)nb)bc=(uint32_t)nb;cases[case_count++]=(struct GuestCase){flags,save(a,ac),(uint32_t)na,save(b,bc),(uint32_t)nb,expected};}
static uint32_t invoke(_cpu *cpu,const uint32_t args[9]){uint32_t top=cpu->_esp,sp=top-36;memcpy(memory+sp,args,36);memset(memory+sp-68,0x79,64);memset(memory+top,0x79,64);cpu->_esp=sp;_cpu before=*cpu;tribes_enter(cpu,1);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));assert(!memcmp(memory+sp,args,36));for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x79&&memory[top+i]==0x79);return result;}
static void setup(_cpu *cpu,unsigned i){memset(cpu,0xa5,sizeof *cpu);cpu->_pointer_offset=(uintptr_t)memory;cpu->_esp=0x100000+65536*i+0xf003;}
static void *worker(void *arg){_cpu cpu;setup(&cpu,(unsigned)(uintptr_t)arg);
 for(unsigned ch=(unsigned)(uintptr_t)arg;ch<65536;ch+=8){uint32_t at=0x220001+2*ch;uint32_t args[]={locale,0,at,1,at,1,0,0,0};tribes_set_last_error(123);assert(invoke(&cpu,args)==2&&tribes_get_last_error()==123);args[1]=2;assert(invoke(&cpu,args)==2&&tribes_get_last_error()==123);}
 for(unsigned k=0;k<25;k++)for(unsigned i=0;i<case_count;i++){struct GuestCase *c=cases+i;uint32_t args[]={k%3?locale:0,c->flags,c->a,c->na,c->b,c->nb,0,0,0};tribes_set_last_error(123);uint32_t value=invoke(&cpu,args);if(value!=c->result){fprintf(stderr,"case %u flags %x returned %u wanted %u\n",i,c->flags,value,c->result);abort();}assert(tribes_get_last_error()==123);args[2]=c->b;args[3]=c->nb;args[4]=c->a;args[5]=c->na;assert(invoke(&cpu,args)==4-c->result&&tribes_get_last_error()==123);}
 return NULL;}
int main(int argc,char **argv){memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory+0x100000,0x80000,PROT_READ|PROT_WRITE)&&!mprotect(memory+0x200000,0x10000,PROT_READ|PROT_WRITE));
 assert(!mprotect(memory+0x220000,0x30000,PROT_READ|PROT_WRITE));for(unsigned i=0;i<65536;i++){uint16_t ch=(uint16_t)i;memcpy(memory+0x220001+2*i,&ch,2);}assert(!mprotect(memory+0x220000,0x30000,PROT_READ));
 static const uint16_t en[]={ 'e','n','-','U','S',0},badlocale[]={'t','r','-','T','R',0};locale=save(en,6);uint32_t bad=save(badlocale,6);
 for(unsigned i=0;i<sizeof wine_cases/sizeof wine_cases[0];i++){const struct Case *c=wine_cases+i;add(c->flags,c->a,c->na,c->b,c->nb,c->result);}
 static const uint16_t abc[]={'A','B','C',0,0xee},acute[]={0xc1,'B','C',0},decomp[]={'A',0x301,'B','C',0},nul[]={'A',0,'B','C'},digit2[]={'f','i','l','e','2',0},digit10[]={'f','i','l','e','1','0',0},hiragana[]={0x3042,0},katakana[]={0x30a2,0},fullwidth[]={0xff21,0},a[]={'A',0},empty[]={0};
 add(0,abc,4,acute,4,1);add(0,acute,4,decomp,5,2);add(2,abc,4,decomp,5,2);add(0,nul,4,abc,4,2);add(0,digit2,-1,digit10,-1,3);add(8,digit2,-1,digit10,-1,1);add(0x10000,hiragana,-1,katakana,-1,2);add(0x20000,fullwidth,-1,a,-1,2);add(0,empty,0,a,-1,1);add(0,empty,0,empty,0,2);
 static uint16_t long_a[4097],long_b[4097];for(unsigned i=0;i<4096;i++)long_a[i]=long_b[i]='a';long_b[4095]='b';add(0,long_a,4096,long_b,4096,1);add(1,long_a,4096,long_a,4096,2);
 unsigned char snapshot[65536];memcpy(snapshot,memory+0x200000,sizeof snapshot);assert(!mprotect(memory+0x200000,0x10000,PROT_READ));_cpu cpu;setup(&cpu,0);struct GuestCase *c=cases;uint32_t args[]={locale,0,c->a,c->na,c->b,c->nb,0,0,0};
 if(argc>1){if(!strcmp(argv[1],"locale"))args[0]=bad;else if(!strcmp(argv[1],"version"))args[6]=c->a;else if(!strcmp(argv[1],"reserved"))args[7]=c->a;else if(!strcmp(argv[1],"handle"))args[8]=1;else if(!strcmp(argv[1],"large"))args[3]=1048577;(void)invoke(&cpu,args);return 99;}
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
 args[1]=0x80;tribes_set_last_error(123);assert(!invoke(&cpu,args)&&tribes_get_last_error()==1004);args[1]=0;args[2]=0;assert(!invoke(&cpu,args)&&tribes_get_last_error()==87);args[2]=UINT32_MAX;args[3]=1;assert(!invoke(&cpu,args)&&tribes_get_last_error()==87);assert(!memcmp(snapshot,memory+0x200000,sizeof snapshot));
 printf("PASS: %u actual ARM64 nine-argument collation calls on eight threads, exhaustive equality of all 65536 UTF-16 code units with/without ignored diacritics, 50 pinned Wine regression cases plus Unicode/diacritic/word-string/numeric/kana/width/empty/long-string cases and reversed comparisons; readonly unaligned UTF-16 input, embedded NULs and bounded counts, full CPU/FP/flags/stack preservation, per-thread LastError and error contracts\n",8*25*case_count*2+3+65536*2);assert(!munmap(memory,UINT64_C(0x100000000)));return 0;}
