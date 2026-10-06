#include "../port/core/guest_locale_callback.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tbimp_EnumSystemLocalesEx(void),fixture_callback(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){if(pc==1)return (void *)tbimp_EnumSystemLocalesEx;if(pc==2)return (void *)fixture_callback;if(pc==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
#include "locale_expected.h"
static unsigned char *memory;
struct Context {uint32_t mask,hash,count,limit,parameter;int nested;};
static _Thread_local struct Context *current;
static _Thread_local unsigned callbacks;
static uint32_t step(uint32_t hash,unsigned byte){return (hash^byte)*16777619u;}
uint32_t fixture_body(_cpu *cpu,uint32_t pointer,uint32_t flags,uint32_t parameter){
 struct Context *c=current;assert(c&&parameter==c->parameter&&(c->mask==0||(flags&c->mask)));callbacks++;unsigned i=0;uint16_t ch;
 do{memcpy(&ch,memory+pointer+2*i,2);assert(ch<128&&i<85);c->hash=step(c->hash,ch&255);c->hash=step(c->hash,ch>>8);i++;}while(ch);
 for(i=0;i<4;i++)c->hash=step(c->hash,(flags>>(8*i))&255);c->count++;
 if(c->nested&&c->count==1){struct Context inner={4,2166136261u,0,3,c->parameter|0x80000000,0};current=&inner;assert(tribes_enum_system_locales_ex(cpu,2,4,inner.parameter,0));assert(inner.count==3&&inner.hash==expected[4].prefix_hash);current=c;}
 /* The API supplies a writable temporary name; modifications must not affect
    subsequent names, concurrent callers or the immutable provider table. */
 uint16_t mutation=0x7fff;memcpy(memory+pointer,&mutation,2);cpu->_ecx=0xdeadbeef;cpu->_eflags=0x246;cpu->_st_cw=0xb7f;tribes_set_last_error(80+(parameter&7));return !c->limit||c->count<c->limit;
}
static uint32_t invoke(_cpu *cpu,const uint32_t args[4]){uint32_t top=cpu->_esp,sp=top-16;memcpy(memory+sp,args,16);memset(memory+sp-68,0x79,64);memset(memory+top,0x79,64);cpu->_esp=sp;_cpu before=*cpu;unsigned old=callbacks;tribes_enter(cpu,1);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;if(callbacks>old)before._st_cw=0xb7f;assert(!memcmp(&before,cpu,sizeof before));assert(!memcmp(memory+sp,args,16));for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x79&&memory[top+i]==0x79);return result;}
static void setup(_cpu *cpu,unsigned i){memset(cpu,0xa5,sizeof *cpu);cpu->_pointer_offset=(uintptr_t)memory;cpu->_esp=0x100000+65536*i+0xf003;cpu->stack_bottom=memory+0x100000+65536*i;cpu->stack_top=memory+0x110000+65536*i;}
static void *worker(void *arg){unsigned id=(unsigned)(uintptr_t)arg;_cpu cpu;setup(&cpu,id);
 for(unsigned round=0;round<3;round++)for(uint32_t mask=0;mask<64;mask++){if(mask&~0x37u)continue;struct Context c={mask,2166136261u,0,round==1?3:0,0x12340000|id,round==2};current=&c;uint32_t args[]={2,mask,c.parameter,0};tribes_set_last_error(123);assert(invoke(&cpu,args));uint32_t count=c.limit&&expected[mask].count>c.limit?c.limit:expected[mask].count;assert(c.count==count&&c.hash==(c.limit?expected[mask].prefix_hash:expected[mask].hash));assert(tribes_get_last_error()==(count?80+id:123));}
 return NULL;}
int main(int argc,char **argv){memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED&&!mprotect(memory+0x100000,0x80000,PROT_READ|PROT_WRITE));_cpu cpu;setup(&cpu,0);uint32_t args[]={2,0,0,0};
 if(argc>1){assert(!strcmp(argv[1],"stack"));cpu.stack_bottom=memory+cpu._esp-128;(void)invoke(&cpu,args);return 99;}
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));current=NULL;
 args[0]=0;assert(!invoke(&cpu,args)&&tribes_get_last_error()==87);args[0]=2;args[3]=1;assert(!invoke(&cpu,args)&&tribes_get_last_error()==87);args[3]=0;args[1]=8;assert(!invoke(&cpu,args)&&tribes_get_last_error()==1004);args[1]=0x80000000;assert(!invoke(&cpu,args)&&tribes_get_last_error()==1004);
 assert(!munmap(memory,UINT64_C(0x100000000)));puts("PASS: 768 actual ARM64 four-argument locale enumeration calls on eight threads; full ordered 958-name profile, all 32 valid filter combinations, first-three early stops, nested actual three-argument stdcall callbacks, callback name mutation isolation, thread LastError, shared FP state and preserved caller CPU/flags/stack; invalid callback/reserved/flags and stack-bound fault");return 0;}
