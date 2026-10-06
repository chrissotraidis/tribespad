#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/performance_clock.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
extern void tbimp_Sleep(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){if(pc==1)return (void *)tbimp_Sleep;if(pc==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static atomic_uint interrupts,signals_seen,ready,finished,worker_progress;
void tribes_sleep_interruption(void){atomic_fetch_add(&interrupts,1);}
static void signal_handler(int signal){(void)signal;atomic_fetch_add(&signals_seen,1);}
static uint64_t now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static uint64_t invoke(_cpu *cpu,uint32_t delay){
    unsigned char *m=(void *)(uintptr_t)cpu->_pointer_offset;uint32_t top=cpu->_esp,sp=top-4;
    memcpy(m+sp,&delay,4);memset(m+sp-68,0x71,64);memset(m+top,0x71,64);cpu->_esp=sp;_cpu before=*cpu;
    tribes_set_last_error(123);uint64_t started=now();tribes_enter(cpu,1);uint64_t elapsed=now()-started;
    before._esp=top;assert(!memcmp(&before,cpu,sizeof before));assert(tribes_get_last_error()==123);
    uint32_t original;memcpy(&original,m+sp,4);assert(original==delay);
    for(unsigned i=0;i<64;i++)assert(m[sp-68+i]==0x71&&m[top+i]==0x71);
    assert(elapsed>=(uint64_t)delay*1000000);return elapsed;
}
static void *sleeper(void *context){
    _cpu *cpu=context;atomic_store(&ready,1);(void)invoke(cpu,40);atomic_store(&finished,1);return NULL;
}
static void *worker(void *context){
    (void)context;tribes_set_last_error(91);
    while(!atomic_load(&finished)){atomic_fetch_add(&worker_progress,1);tribes_sleep(0);assert(tribes_get_last_error()==91);}return NULL;
}
int main(int argc,char **argv){
    (void)argv;if(argc>1){tribes_sleep(UINT32_MAX);return 99;}
    unsigned char *m=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED);assert(!mprotect(m+0x100000,65536,PROT_READ|PROT_WRITE));
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)m;cpu._esp=0x10f003;
    for(unsigned i=0;i<64;i++)(void)invoke(&cpu,0);
    for(unsigned i=0;i<20;i++)(void)invoke(&cpu,1);
    assert(atomic_is_lock_free(&signals_seen));struct sigaction action;memset(&action,0,sizeof action);action.sa_handler=signal_handler;assert(!sigemptyset(&action.sa_mask));assert(!sigaction(SIGUSR1,&action,NULL));
    for(unsigned i=0;i<3;i++){
        atomic_store(&ready,0);atomic_store(&finished,0);unsigned old_interrupts=atomic_load(&interrupts);pthread_t sleeping,working;
        assert(!pthread_create(&working,NULL,worker,NULL));assert(!pthread_create(&sleeping,NULL,sleeper,&cpu));
        while(!atomic_load(&ready))tribes_sleep(0);
        for(unsigned signal=0;signal<3;signal++){tribes_sleep(1);if(!atomic_load(&finished))assert(!pthread_kill(sleeping,SIGUSR1));}
        assert(!pthread_join(sleeping,NULL));assert(!pthread_join(working,NULL));assert(atomic_load(&interrupts)>old_interrupts);
    }
    assert(atomic_load(&signals_seen)&&atomic_load(&worker_progress));assert(!munmap(m,UINT64_C(0x100000000)));
    printf("PASS: 64 zero-yield and 20 one-millisecond actual ARM64 void stdcall calls; 3 original-sized 40 ms waits survive native EINTR (%u restarts), concurrent native thread progresses (%u yields), EAX/all CPU/flags/stack/argument guards and thread-local LastError preserved; native monotonic elapsed bounds, no Windows tick/priority/APC parity claim\n",atomic_load(&interrupts),atomic_load(&worker_progress));return 0;
}
