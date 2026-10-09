#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_slist.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tbimp_InterlockedFlushSList(void),tbimp_InitializeSListHead(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
static uint64_t race_base;static _Atomic unsigned winners;
static void *flush_worker(void *unused){(void)unused;uint32_t first=tribes_flush_slist(race_base,0x100010);if(first){assert(first==0x101000);atomic_fetch_add(&winners,1);}return NULL;}
int main(int argc,char **argv) {
    (void)argv;
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    unsigned char *memory=window;
#ifdef CHECKED_TEST
    assert(tribes_memory_checks_configure((uintptr_t)window,UINT64_C(0x100000000)));
    assert(tribes_memory_permissions_set(0,UINT64_C(0x100000000),0));assert(tribes_memory_permissions_set(0x100000,0x10000,3));
    tribes_memory_external_configure((uintptr_t)window,UINT64_C(0x100000000));
    const void *targets[]={tbimp_InitializeSListHead,tbimp_InterlockedFlushSList,tribes_return_to_host};tribes_memory_register_targets(targets,3);tribes_memory_external_enable();
#endif
    if(argc>1){uint32_t bad=0x110000;(void)invoke(&cpu,2,1,&bad);abort();}

    for(unsigned i=0;i<1000;i++) {
        /* Alternate 16-byte and eight-only alignment to distinguish x86/Win64. */
        uint32_t head=0x100010+(i%32)*8;memset(memory+head-8,0x79,32);memset(memory+head,(i%254)+1,8);
        cpu._eax=i*73+17;tribes_set_last_error(123);uint32_t before=cpu._eax;
        assert(invoke(&cpu,1,1,&head)==before&&tribes_get_last_error()==123);
        for(unsigned n=0;n<8;n++)assert(memory[head+n]==0);
        for(unsigned n=0;n<8;n++)assert(memory[head-8+n]==0x79);
        for(unsigned n=8;n<24;n++)assert(memory[head+n]==0x79);
    }
    for(unsigned i=0;i<1000;i++){
        uint32_t head=0x100010+(i%32)*8;uint64_t before=((uint64_t)(uint16_t)(65500+i)<<48)|(UINT64_C(3)<<32)|0x101000;
        memcpy(memory+head,&before,8);tribes_set_last_error(123);
        assert(invoke(&cpu,2,1,&head)==0x101000&&tribes_get_last_error()==123);
        uint64_t after;memcpy(&after,memory+head,8);assert(after==(((before>>48)+1)<<48));
        assert(invoke(&cpu,2,1,&head)==0);assert(!memcmp(memory+head,&after,8));
    }
    uint64_t populated=(UINT64_C(11)<<48)|(UINT64_C(3)<<32)|0x101000;memcpy(memory+0x100010,&populated,8);race_base=(uintptr_t)window;
    pthread_t workers[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&workers[i],NULL,flush_worker,NULL));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(workers[i],NULL));assert(winners==1);
    /* Independent guest image: no host/global list registry or native pointers. */
    void *other=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(other!=MAP_FAILED);
    assert(!mprotect((char *)other+0x100000,0x10000,PROT_READ|PROT_WRITE));
    memset((char *)other+0x100010,0x81,8);memset(memory+0x100010,0x73,8);cpu._pointer_offset=(uintptr_t)other;uint32_t head=0x100010;
    assert(invoke(&cpu,1,1,&head)==cpu._eax);
    for(unsigned i=0;i<8;i++)assert(!*((unsigned char *)other+head+i)&&memory[head+i]==0x73);
    assert(!munmap(other,UINT64_C(0x100000000))&&!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 1001 actual ARM64 void stdcall SLIST initializations, eight-byte x86 layout/alignment, neighboring memory/LastError/CPU/EAX/flags/stack preservation and independent guest images; 2000 populated/empty flushes with sequence rollover, eight racing flushers with one owner; push/pop remain unsupported");return 0;
}
