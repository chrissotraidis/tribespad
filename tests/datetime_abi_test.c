#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
extern void tbimp_GetDateFormatEx(void),tbimp_GetTimeFormatEx(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){if(pc==1)return (void *)tbimp_GetDateFormatEx;if(pc==2)return (void *)tbimp_GetTimeFormatEx;if(pc==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
struct FormatCase {uint32_t target,flags;const uint16_t *locale,*format;uint16_t time[8];const uint16_t *expected;};
#include "datetime_cases.h"
static unsigned char *memory;static uint32_t cursor=0x200001;
static struct {uint32_t target,flags,locale,format,time,expected,count;} cases[100];
static uint32_t save(const void *p,unsigned n){uint32_t at=cursor;assert(at+n<0x210000);memcpy(memory+at,p,n);cursor+=n+3;return at;}
static unsigned length(const uint16_t *p){unsigned n=0;while(p[n])n++;return n+1;}
static uint32_t invoke(_cpu *cpu,uint32_t target,const uint32_t args[7]){unsigned count=target==1?7:6;uint32_t top=cpu->_esp,sp=top-4*count;memcpy(memory+sp,args,4*count);memset(memory+sp-68,0x79,64);memset(memory+top,0x79,64);cpu->_esp=sp;_cpu before=*cpu;tribes_enter(cpu,target);uint32_t value=cpu->_eax;before._eax=value;before._esp=top;assert(!memcmp(cpu,&before,sizeof before)&&!memcmp(memory+sp,args,4*count));for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x79&&memory[top+i]==0x79);return value;}
static void setup(_cpu *cpu,unsigned i){memset(cpu,0xa5,sizeof *cpu);cpu->_pointer_offset=(uintptr_t)memory;cpu->_esp=0x100000+65536*i+0xf003;}
static void *worker(void *p){unsigned id=(unsigned)(uintptr_t)p;_cpu cpu;setup(&cpu,id);uint32_t output=0x100001+id*65536+0x8000;
 for(unsigned k=0;k<10;k++)for(unsigned i=0;i<sizeof format_cases/sizeof format_cases[0];i++){uint32_t args[]={cases[i].locale,cases[i].flags,cases[i].time,cases[i].format,output,0,0};tribes_set_last_error(123);memset(memory+output-1,0x79,2048);uint32_t n=invoke(&cpu,cases[i].target,args);if(n!=cases[i].count){fprintf(stderr,"case %u query %u wanted %u\n",i,n,cases[i].count);abort();}assert(tribes_get_last_error()==123&&memory[output]==0x79);args[5]=n;assert(invoke(&cpu,cases[i].target,args)==n);if(memcmp(memory+output,memory+cases[i].expected,2*n)){fprintf(stderr,"case %u output mismatch: ",i);for(unsigned k=0;k<n;k++){uint16_t v;memcpy(&v,memory+output+2*k,2);fprintf(stderr,"%04x ",v);}fprintf(stderr,"\n");abort();}assert(memory[output-1]==0x79&&memory[output+2*n]==0x79&&tribes_get_last_error()==123);
 args[5]=n>1?n-1:1;if(n>1){assert(!invoke(&cpu,cases[i].target,args)&&tribes_get_last_error()==122);assert(memory[output-1]==0x79&&memory[output+2*n]==0x79);} }
 return NULL;}
int main(int argc,char **argv){memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED&&!mprotect(memory+0x100000,0x80000,PROT_READ|PROT_WRITE)&&!mprotect(memory+0x200000,0x10000,PROT_READ|PROT_WRITE));
 for(unsigned i=0;i<sizeof format_cases/sizeof format_cases[0];i++){const struct FormatCase *c=format_cases+i;cases[i].target=c->target;cases[i].flags=c->flags;cases[i].locale=save(c->locale,2*length(c->locale));cases[i].format=c->format?save(c->format,2*length(c->format)):0;cases[i].time=save(c->time,16);cases[i].expected=save(c->expected,2*length(c->expected));cases[i].count=length(c->expected);}
 static const uint16_t th[]={'t','h','-','T','H',0};uint32_t thai=save(th,sizeof th);uint16_t large[4097];for(unsigned i=0;i<4097;i++)large[i]='z';uint32_t long_format=save(large,sizeof large);unsigned char snapshot[65536];memcpy(snapshot,memory+0x200000,sizeof snapshot);assert(!mprotect(memory+0x200000,0x10000,PROT_READ));_cpu cpu;setup(&cpu,0);uint32_t args[]={cases[0].locale,0,cases[0].time,cases[0].format,0x108001,100,0};
 if(argc>1){if(!strcmp(argv[1],"calendar"))args[0]=thai;else if(!strcmp(argv[1],"layout"))args[1]=16;else if(!strcmp(argv[1],"long-format"))args[3]=long_format;(void)invoke(&cpu,1,args);return 99;}
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(!memcmp(snapshot,memory+0x200000,sizeof snapshot));
 args[6]=1;assert(!invoke(&cpu,1,args)&&tribes_get_last_error()==87);args[6]=0;args[1]=3;assert(!invoke(&cpu,1,args)&&tribes_get_last_error()==1004);args[1]=0x100;assert(!invoke(&cpu,1,args)&&tribes_get_last_error()==1004);args[1]=0;args[4]=0;assert(!invoke(&cpu,1,args)&&tribes_get_last_error()==87);args[4]=0x108001;args[5]=UINT32_MAX;assert(!invoke(&cpu,1,args)&&tribes_get_last_error()==87);
 uint16_t bad[8]={1900,2,0,29,0,0,0,0};uint32_t stamp=0x109001;memcpy(memory+stamp,bad,16);args[2]=stamp;args[5]=100;assert(!invoke(&cpu,1,args)&&tribes_get_last_error()==87);bad[0]=1600;bad[1]=12;bad[3]=31;memcpy(memory+stamp,bad,16);assert(!invoke(&cpu,1,args)&&tribes_get_last_error()==87);bad[4]=24;memcpy(memory+stamp,bad,16);assert(!invoke(&cpu,2,args)&&tribes_get_last_error()==87);
 args[2]=0;args[3]=0;args[0]=0;args[5]=0;tribes_set_last_error(123);assert(invoke(&cpu,1,args)>1&&tribes_get_last_error()==123);assert(invoke(&cpu,2,args)>1&&tribes_get_last_error()==123);
 printf("PASS: %zu golden date/time cases through actual ARM64 seven/six-argument wrappers, eight concurrent threads, size queries/full/short buffers, readonly unaligned UTF-16/SYSTEMTIME inputs, Unicode locale/genitive/default/custom patterns, quotes/time filtering/leap dates/DOW recomputation/long output, native local-clock queries; full CPU/FP/flags/stack guards and LastError, invalid fields/flags/capacity/reserved cases\n",sizeof format_cases/sizeof format_cases[0]);assert(!munmap(memory,UINT64_C(0x100000000)));return 0;}
