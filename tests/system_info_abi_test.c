#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "../port/core/guest_system.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
extern void tribes_enter(_cpu *,uint32_t);
extern void x86_cpuid(_cpu *);
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));unsigned char *memory=window;
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    _cpu identity;memset(&identity,0,sizeof identity);identity._eax=1;x86_cpuid(&identity);uint32_t signature=identity._eax;
    long online=sysconf(_SC_NPROCESSORS_ONLN);assert(online>0);uint32_t count=online>32?32:(uint32_t)online;
#ifdef CHECKED_TEST
    assert(tribes_memory_checks_configure((uintptr_t)window,UINT64_C(0x100000000)));
    tribes_memory_external_configure((uintptr_t)window,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
    for(unsigned service=1;service<=2;service++)for(unsigned i=0;i<1000;i++) {
        uint32_t output=0x100801+i%3;memset(memory+output-1,0x79,40);tribes_set_last_error(123);cpu._eax=i*73+17;uint32_t saved=cpu._eax;
        assert(invoke(&cpu,service,1,&output)==saved&&tribes_get_last_error()==123);
        uint32_t words[9];memcpy(words,memory+output,36);
        assert(!words[0]&&words[1]==4096&&words[2]==0x10000&&words[3]==0x7ffeffff&&words[7]==0x10000);
        assert(words[6]==586&&(words[8]&0xffff)==((signature>>8)&15)&&words[8]>>16==(((signature>>4)&15)<<8|(signature&15)));
        assert(words[5]==count&&__builtin_popcount(words[4])==(int)count&&words[4]==(count==32?UINT32_MAX:(1u<<count)-1));
        assert(memory[output-1]==0x79);for(unsigned n=36;n<39;n++)assert(memory[output+n]==0x79);
    }
#ifdef CHECKED_TEST
    for(unsigned service=1;service<=2;service++)for(unsigned crossing=0;crossing<2;crossing++){
        memset(memory+0x107fd0,0x79,128);pid_t child=fork();assert(child>=0);
        if(!child){struct rlimit lim={0,0};setrlimit(RLIMIT_CORE,&lim);assert(tribes_memory_permissions_set(0x108000,4096,1));
            uint32_t output=crossing?0x107ff0:0x108000;invoke(&cpu,service,1,&output);_exit(99);}
        int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
        /* A void API with an invalid output has no transactional-write
           guarantee. Stores to the valid prefix may precede the first rejected
           store, but protected bytes and both surrounding guards stay intact. */
        for(unsigned n=0;n<128;n++)if(!crossing||n<32||n>=48)assert(memory[0x107fd0+n]==0x79);
    }
#endif
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 2000 actual ARM64 GetSystemInfo/GetNativeSystemInfo void stdcall calls, exact 36-byte x86 pointer/mask layout, unaligned guards, CPUID family/model/stepping coherence, native processor count/mask, logical 4 KiB/64 KiB guest geometry, EAX/CPU/flags/stack/LastError preservation; x86-only compatibility architecture");return 0;
}
