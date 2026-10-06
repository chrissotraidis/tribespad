#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "../port/core/guest_modules.h"
#include "../port/core/guest_filesystem.h"
extern void tbimp_GetCurrentThread(void),tbimp_GetCurrentProcess(void),tbimp_CloseHandle(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_GetCurrentThread;if(address==2)return (void *)tbimp_GetCurrentProcess;if(address==3)return (void *)tbimp_CloseHandle;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
static void *worker(void *unused) {
    (void)unused;
    unsigned char *memory=mmap(NULL,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x1f003;
    for(unsigned i=0;i<1000;i++) {
        tribes_set_last_error(123);
        uint32_t thread=invoke(&cpu,1,0,NULL),process=invoke(&cpu,2,0,NULL);
        assert(thread==UINT32_MAX-1&&process==UINT32_MAX&&tribes_get_last_error()==123);
        assert(invoke(&cpu,3,1,&thread)&&invoke(&cpu,3,1,&process)&&tribes_get_last_error()==123);
        assert(invoke(&cpu,1,0,NULL)==thread&&invoke(&cpu,2,0,NULL)==process);
    }
    uint32_t invalid=0;assert(!invoke(&cpu,3,1,&invalid)&&tribes_get_last_error()==6);
    assert(!munmap(memory,0x20000));return NULL;
}
int main(void) {
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: 8000 actual ARM64 current-thread/process pseudo-handle sequences and no-effect closes across eight native threads; zero-argument ABI, unaligned stack guards, CPU/flags, thread-local LastError and invalid-handle rejection; thread-object consumers/duplication remain unsupported");return 0;
}
