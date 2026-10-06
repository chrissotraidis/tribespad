#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
extern void tbimp_GetLocaleInfoEx(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){if(pc==1)return (void *)tbimp_GetLocaleInfoEx;if(pc==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
struct InfoCase {const uint16_t *locale;uint32_t type;const unsigned char *expected;unsigned bytes;};
#include "locale_info_cases.h"
static unsigned char *memory;static uint32_t cursor=0x200001;
static struct {uint32_t locale,type,expected,count;} cases[1024];
static uint32_t save(const void *p,unsigned n){uint32_t at=cursor;assert(at+n<0x240000);memcpy(memory+at,p,n);cursor+=n+3;return at;}
static unsigned length(const uint16_t *p){unsigned n=0;while(p[n])n++;return n+1;}
static uint32_t invoke(_cpu *cpu,const uint32_t args[4]){uint32_t top=cpu->_esp,sp=top-16;memcpy(memory+sp,args,16);memset(memory+sp-68,0x79,64);memset(memory+top,0x79,64);cpu->_esp=sp;_cpu before=*cpu;tribes_enter(cpu,1);uint32_t value=cpu->_eax;before._eax=value;before._esp=top;assert(!memcmp(cpu,&before,sizeof before)&&!memcmp(memory+sp,args,16));for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x79&&memory[top+i]==0x79);return value;}
static void setup(_cpu *cpu,unsigned i){memset(cpu,0xa5,sizeof *cpu);cpu->_pointer_offset=(uintptr_t)memory;cpu->_esp=0x100000+65536*i+0xf003;}
static void *worker(void *p){unsigned id=(unsigned)(uintptr_t)p;_cpu cpu;setup(&cpu,id);uint32_t output=0x100001+id*65536+0x8000;
 for(unsigned k=0;k<5;k++)for(unsigned i=0;i<sizeof info_cases/sizeof info_cases[0];i++){uint32_t args[]={cases[i].locale,cases[i].type,output,0};tribes_set_last_error(123);memset(memory+output-1,0x79,2048);uint32_t n=invoke(&cpu,args);if(n!=cases[i].count){fprintf(stderr,"case %u type %x query %u wanted %u error %u\n",i,args[1],n,cases[i].count,tribes_get_last_error());abort();}assert(tribes_get_last_error()==123&&memory[output]==0x79);args[3]=n;assert(invoke(&cpu,args)==n);if(memcmp(memory+output,memory+cases[i].expected,2*n)){fprintf(stderr,"case %u type %x output mismatch:",i,args[1]);for(unsigned j=0;j<n*2;j++)fprintf(stderr," %02x",memory[output+j]);fprintf(stderr,"\n");abort();}assert(memory[output-1]==0x79&&memory[output+2*n]==0x79&&tribes_get_last_error()==123);
 if(n>1){args[3]=n-1;memset(memory+output-1,0x79,2048);assert(!invoke(&cpu,args)&&tribes_get_last_error()==122);for(unsigned j=0;j<2048;j++)assert(memory[output-1+j]==0x79);}}
 return NULL;}
int main(int argc,char **argv){memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED&&!mprotect(memory+0x100000,0x80000,PROT_READ|PROT_WRITE)&&!mprotect(memory+0x200000,0x40000,PROT_READ|PROT_WRITE));assert(sizeof info_cases/sizeof info_cases[0]<=1024);
 for(unsigned i=0;i<sizeof info_cases/sizeof info_cases[0];i++){const struct InfoCase *c=info_cases+i;cases[i].locale=c->locale?save(c->locale,2*length(c->locale)):0;cases[i].type=c->type;cases[i].expected=save(c->expected,c->bytes);cases[i].count=c->bytes/2;assert(!(c->bytes&1));}
 static const uint16_t unknown[]={'d','u','m','m','y',0},unicode[]={0x100,0},long_name[85]={[0 ... 84]='z'};uint32_t badname=save(unknown,sizeof unknown),nonascii=save(unicode,sizeof unicode),longname=save(long_name,sizeof long_name);unsigned char *snapshot=malloc(0x40000);assert(snapshot);memcpy(snapshot,memory+0x200000,0x40000);assert(!mprotect(memory+0x200000,0x40000,PROT_READ));_cpu cpu;setup(&cpu,0);uint32_t args[]={cases[0].locale,0x5c,0x108001,100};
 if(argc>1){assert(!strcmp(argv[1],"sort-name"));args[1]=0x1013;(void)invoke(&cpu,args);return 99;}
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(!memcmp(snapshot,memory+0x200000,0x40000));free(snapshot);
 for(unsigned i=0;i<3;i++){args[0]=i==0?badname:i==1?nonascii:longname;assert(!invoke(&cpu,args)&&tribes_get_last_error()==87);}args[0]=cases[0].locale;
 args[1]=0;assert(!invoke(&cpu,args)&&tribes_get_last_error()==1004);args[1]=0x0800005c;assert(!invoke(&cpu,args)&&tribes_get_last_error()==1004);args[1]=0x2000005c;assert(!invoke(&cpu,args)&&tribes_get_last_error()==1004);args[1]=0x5c;args[2]=0;assert(!invoke(&cpu,args)&&tribes_get_last_error()==87);args[2]=0x108001;args[3]=UINT32_MAX;assert(!invoke(&cpu,args)&&tribes_get_last_error()==87);args[2]=1;args[3]=100;assert(!invoke(&cpu,args)&&tribes_get_last_error()==87);args[2]=1;args[3]=0;tribes_set_last_error(123);assert(invoke(&cpu,args)==1&&tribes_get_last_error()==123);
 printf("PASS: %zu golden locale information cases through actual ARM64 four-argument wrappers, eight concurrent threads, size/full/short buffers with failed-call write isolation, readonly unaligned Unicode names, invariant/neutral/specific/alias/default locales, genitive names, numeric DWORD and binary font signature outputs, full CPU/FP/flags/stack guards and LastError, invalid fields/flags/capacity/name/range errors\n",sizeof info_cases/sizeof info_cases[0]);assert(!munmap(memory,UINT64_C(0x100000000)));return 0;}
