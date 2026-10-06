#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_srw.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sched.h>
extern void tbimp_InitializeSRWLock(void),tbimp_AcquireSRWLockExclusive(void),tbimp_ReleaseSRWLockExclusive(void),tbimp_AcquireSRWLockShared(void),tbimp_ReleaseSRWLockShared(void),tbimp_TryAcquireSRWLockExclusive(void),tbimp_TryAcquireSRWLockShared(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){switch(pc){case 1:return (void *)tbimp_InitializeSRWLock;case 2:return (void *)tbimp_AcquireSRWLockExclusive;case 3:return (void *)tbimp_ReleaseSRWLockExclusive;case 4:return (void *)tbimp_AcquireSRWLockShared;case 5:return (void *)tbimp_ReleaseSRWLockShared;case 6:return (void *)tbimp_TryAcquireSRWLockExclusive;case 7:return (void *)tbimp_TryAcquireSRWLockShared;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,uint32_t pointer){
    unsigned char *m=(void *)(uintptr_t)cpu->_pointer_offset;uint32_t top=cpu->_esp,sp=top-4;memcpy(m+sp,&pointer,4);memset(m+sp-68,0x71,64);memset(m+top,0x71,64);cpu->_esp=sp;_cpu before=*cpu;
    tribes_set_last_error(123);tribes_enter(cpu,target);uint32_t result=cpu->_eax;if(target>=6)before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));assert(tribes_get_last_error()==123);
    uint32_t original;memcpy(&original,m+sp,4);assert(original==pointer);for(unsigned i=0;i<64;i++)assert(m[sp-68+i]==0x71&&m[top+i]==0x71);return result;
}
static uint32_t word(unsigned char *m,uint32_t p){uint32_t v;memcpy(&v,m+p,4);return v;}
static _cpu cpu_for(uint64_t base,unsigned slot){_cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x20f003+slot*65536;return cpu;}
struct Worker {uint64_t base;unsigned slot;int writer;uint32_t lock;};
struct Data {uint64_t value,inverse;};
static void *worker(void *context){
    struct Worker *w=context;_cpu cpu=cpu_for(w->base,w->slot);struct Data *data=(void *)(uintptr_t)(w->base+0x100a00);
    for(unsigned i=0;i<1000;i++){
        (void)invoke(&cpu,w->writer?2:4,w->lock);assert(data->inverse==~data->value);
        if(w->writer){++data->value;data->inverse=~data->value;}
        (void)invoke(&cpu,w->writer?3:5,w->lock);
    }return NULL;
}
static atomic_uint held,released,writer_entered;
static void *held_reader(void *context){struct Worker *w=context;_cpu cpu=cpu_for(w->base,w->slot);(void)invoke(&cpu,4,w->lock);atomic_fetch_add(&held,1);while(!atomic_load(&released))sched_yield();(void)invoke(&cpu,5,w->lock);return NULL;}
static void *waiting_writer(void *context){struct Worker *w=context;_cpu cpu=cpu_for(w->base,w->slot);(void)invoke(&cpu,2,w->lock);atomic_store(&writer_entered,1);(void)invoke(&cpu,3,w->lock);return NULL;}
static void *wrong_release(void *context){struct Worker *w=context;tribes_srw_release_shared(w->base,w->lock);return NULL;}
int main(int argc,char **argv){
    unsigned char *m=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED);assert(!mprotect(m+0x100000,65536,PROT_READ|PROT_WRITE));assert(!mprotect(m+0x200000,16*65536,PROT_READ|PROT_WRITE));uint64_t base=(uintptr_t)m;_cpu cpu=cpu_for(base,0);uint32_t lock=0x100901;
    if(argc>1){
        if(!strcmp(argv[1],"null")){(void)invoke(&cpu,2,0);}
        else if(!strcmp(argv[1],"overflow")){(void)invoke(&cpu,2,UINT32_MAX-2);}
        else if(!strcmp(argv[1],"nonzero")){memset(m+lock,0x71,4);(void)invoke(&cpu,2,lock);}
        else if(!strcmp(argv[1],"release")){(void)invoke(&cpu,3,lock);}
        else if(!strcmp(argv[1],"recursion")){(void)invoke(&cpu,2,lock);(void)invoke(&cpu,2,lock);}
        else if(!strcmp(argv[1],"upgrade")){(void)invoke(&cpu,4,lock);(void)invoke(&cpu,2,lock);}
        else if(!strcmp(argv[1],"shared-recursion")){(void)invoke(&cpu,4,lock);(void)invoke(&cpu,7,lock);}
        else if(!strcmp(argv[1],"busy-init")){(void)invoke(&cpu,2,lock);(void)invoke(&cpu,1,lock);}
        else if(!strcmp(argv[1],"copied-live")){(void)invoke(&cpu,2,lock);memcpy(m+lock+16,m+lock,4);(void)invoke(&cpu,2,lock+16);}
        else if(!strcmp(argv[1],"wrong-owner")){(void)invoke(&cpu,4,lock);struct Worker w={base,1,0,lock};pthread_t thread;assert(!pthread_create(&thread,NULL,wrong_release,&w));assert(!pthread_join(thread,NULL));}
        else abort();return 99;
    }
    for(unsigned i=0;i<500;i++){
        memset(m+lock-1,0x71,6);(void)invoke(&cpu,1,lock);assert(!word(m,lock));
        (void)invoke(&cpu,2,lock);assert(!invoke(&cpu,6,lock)&&!invoke(&cpu,7,lock));(void)invoke(&cpu,3,lock);assert(!word(m,lock)&&!tribes_srw_live_count());
        assert(invoke(&cpu,7,lock));assert(!invoke(&cpu,6,lock));(void)invoke(&cpu,5,lock);
        (void)invoke(&cpu,4,lock);(void)invoke(&cpu,5,lock);assert(invoke(&cpu,6,lock));(void)invoke(&cpu,3,lock);
        assert(!word(m,lock)&&!tribes_srw_live_count());assert(m[lock-1]==0x71&&m[lock+4]==0x71);
        /* Idle initial state can be copied/moved without a native destroy API. */
        memcpy(m+lock+16,m+lock,4);(void)invoke(&cpu,2,lock+16);(void)invoke(&cpu,3,lock+16);assert(!tribes_srw_live_count());
    }
    struct Worker reader={base,1,0,lock},writer={base,2,1,lock};pthread_t reading,writing;
    (void)invoke(&cpu,4,lock);assert(!pthread_create(&reading,NULL,held_reader,&reader));while(!atomic_load(&held))sched_yield();
    assert(!pthread_create(&writing,NULL,waiting_writer,&writer));while(!tribes_srw_waiters(base,lock))sched_yield();assert(!atomic_load(&writer_entered));
    (void)invoke(&cpu,5,lock);assert(!atomic_load(&writer_entered));atomic_store(&released,1);assert(!pthread_join(reading,NULL));assert(!pthread_join(writing,NULL));assert(atomic_load(&writer_entered)&&!tribes_srw_live_count()&&!word(m,lock));
    struct Data *data=(void *)(uintptr_t)(base+0x100a00);data->value=0;data->inverse=UINT64_MAX;pthread_t threads[8];struct Worker workers[8];
    for(unsigned i=0;i<8;i++){workers[i]=(struct Worker){base,i+1,i<4,lock};assert(!pthread_create(&threads[i],NULL,worker,&workers[i]));}
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(data->value==4000&&data->inverse==~data->value&&!tribes_srw_live_count()&&!word(m,lock));
    assert(!munmap(m,UINT64_C(0x100000000)));puts("PASS: 500 actual ARM64 seven-service SRW ABI lifecycle sequences, static/dynamic and movable idle zero state, exact 4-byte unaligned guards, try ownership/failure, two simultaneous readers block writer until both release, 16000 concurrent actual ARM64 shared/exclusive calls with 4000 protected writes/paired visibility reads, idle native metadata retirement, void EAX/all CPU/flags/stack/LastError preservation; no fairness/Windows internal bits/recursive shared parity claim");return 0;
}
