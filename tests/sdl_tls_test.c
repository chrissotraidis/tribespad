#include "../port/core/guest_sdl_tls.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t);
extern void sdl_tls_expect(uint32_t,uint32_t),sdl_tls_mutate(void);
extern uint32_t sdl_tls_callbacks(void);
static unsigned char *memory;
static uint32_t call(_cpu *cpu,uint32_t pc,uint32_t id,uint32_t value,uint32_t destructor){
 uint32_t args[]={id,value,destructor};unsigned n=pc==1?3:pc==2?1:0;
 memcpy(memory+cpu->_esp,args,n*4);_cpu before=*cpu;tribes_set_last_error(0x19a5);tribes_enter(cpu,pc);
 if(pc==3)assert(cpu->_eax==before._eax);before._eax=cpu->_eax;
 assert(!memcmp(&before,cpu,sizeof *cpu)&&!memcmp(memory+cpu->_esp,args,n*4)&&tribes_get_last_error()==0x19a5);return cpu->_eax;
}
static _cpu make_cpu(unsigned index){_cpu c={0};c._pointer_offset=(uintptr_t)memory;c._esp=0x20000+0x8000*index;c.stack_bottom=memory+c._esp-0x7000;c.stack_top=memory+c._esp+0x1000;c._eflags=0x246;return c;}
static void *worker(void *arg){
 unsigned index=(unsigned)(uintptr_t)arg;_cpu c=make_cpu(index);uint32_t value=0xa5000000+index;
 assert(call(&c,2,0x1000,0,0)==0);assert(call(&c,1,0x1000,value,4));assert(call(&c,2,0x1000,0,0)==value);
 assert(call(&c,1,0x1000,value+1,4));assert(!sdl_tls_callbacks());sdl_tls_expect(0x1000,value+1);
 _cpu before=c;tribes_set_last_error(0x19a5);tribes_guest_thread_finalize(&c);assert(!memcmp(&before,&c,sizeof c)&&tribes_get_last_error()==0x19a5);assert(sdl_tls_callbacks()==1);assert(call(&c,2,0x1000,0,0)==0);
 /* NULL data still invokes a registered destructor in pinned SDL. */
 sdl_tls_expect(0x1000,0);assert(call(&c,1,0x1000,0,4));call(&c,3,0,0,0);assert(sdl_tls_callbacks()==2);tribes_guest_thread_finalize(&c);assert(sdl_tls_callbacks()==2);return NULL;
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory,0x200000,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));
#ifdef CHECKED
 tribes_memory_external_configure((uintptr_t)memory,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 SDL_SetMainReady();assert(SDL_Init(0));_cpu c=make_cpu(0);
 assert(!call(&c,2,0x1000,0,0));assert(call(&c,1,0x1000,0x1111,4));sdl_tls_expect(0x1000,0x1111);
 if(argc>1){int fault=atoi(argv[1]);if(fault==1){uint32_t forged=0x1234;memcpy(memory+0x1004,&forged,4);call(&c,2,0x1004,0,0);}else if(fault==2){sdl_tls_mutate();call(&c,3,0,0,0);}else if(fault==3){SDL_CleanupTLS();}else if(fault==4){call(&c,2,0x1001,0,0);}return 99;}
 pthread_t threads[24];for(unsigned i=0;i<24;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));for(unsigned i=0;i<24;i++)assert(!pthread_join(threads[i],NULL));
 assert(call(&c,2,0x1000,0,0)==0x1111);assert(!sdl_tls_callbacks());
 /* ID copies refer to the same opaque slot; replacing does not call destructor. */
 memcpy(memory+0x1004,memory+0x1000,4);assert(call(&c,2,0x1004,0,0)==0x1111);assert(call(&c,1,0x1004,0x2222,0));assert(!sdl_tls_callbacks());call(&c,3,0,0,0);assert(!sdl_tls_callbacks());
 assert(!call(&c,1,0,0,0));assert(strstr(SDL_GetError(),"id"));assert(!call(&c,2,0,0,0));assert(strstr(SDL_GetError(),"id"));
 assert(call(&c,1,0x1000,0x3333,4));sdl_tls_expect(0x1000,0x3333);tribes_sdl_tls_cleanup_begin(&c);SDL_Quit();tribes_sdl_tls_cleanup_end();assert(sdl_tls_callbacks()==1);assert(!call(&c,2,0x1000,0,0));tribes_guest_thread_finalize(&c);
 puts("PASS: real SDL TLS,24 isolated guest threads, copied IDs, replacement/null values, cdecl destructors, explicit/finalizer/SDL_Quit cleanup, CPU/LastError preservation");return 0;
}
