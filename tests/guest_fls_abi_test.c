#include "../port/core/guest_fls.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tbimp_FlsAlloc(void),tbimp_FlsFree(void),tbimp_FlsGetValue(void),tbimp_FlsSetValue(void),tribes_return_to_host(void),fixture_fls_callback(void);
extern void tribes_enter(_cpu *,uint32_t);
static unsigned callbacks;
static uint32_t fls_index;
void *tribes_lookup(uint32_t address) {
    switch(address){case 1:return (void *)tbimp_FlsAlloc;case 2:return (void *)tbimp_FlsFree;case 3:return (void *)tbimp_FlsGetValue;case 4:return (void *)tbimp_FlsSetValue;case 5:return (void *)fixture_fls_callback;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}
}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
uint32_t fixture_fls_cleanup(_cpu *cpu,uint32_t value) {
    assert(value&&tribes_global_size(cpu->_pointer_offset,value)>=32);
    /* Actual guest callback reenters another FLS slot; free runs without its registry lock. */
    uint32_t temporary=tribes_fls_alloc(cpu,0);assert(temporary!=UINT32_MAX);
    assert(tribes_fls_set(cpu,temporary,0)&&tribes_fls_free(cpu,temporary));
    assert(!tribes_global_free(cpu->_pointer_offset,value));callbacks++;
    cpu->_ebx=0xdeadbeef;cpu->_eflags=0;return 0xabcdef;
}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before=*cpu;
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
static void *worker(void *argument) {
    _cpu cpu=*(_cpu *)argument;
    uint32_t value=tribes_global_alloc(cpu._pointer_offset,0,32);assert(value);
    uint32_t args[]={fls_index,value};assert(invoke(&cpu,4,2,args));assert(invoke(&cpu,3,1,&fls_index)==value);
    return NULL; /* Native pthread exit executes the emitted guest cleanup using an owned stack. */
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));
    _cpu cpu;memset(&cpu,0,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;cpu._ebx=0x1234;cpu._eflags=0x202;
    cpu.stack_bottom=(char *)window+0x100000;cpu.stack_top=(char *)window+0x110000;
    uint32_t cb=5;tribes_set_last_error(123);fls_index=invoke(&cpu,1,1,&cb);assert(fls_index!=UINT32_MAX&&tribes_get_last_error()==123);
    pthread_t thread;assert(!pthread_create(&thread,NULL,worker,&cpu));assert(!pthread_join(thread,NULL));assert(callbacks==1);
    uint32_t value=tribes_global_alloc(cpu._pointer_offset,0,32);assert(value);uint32_t args[]={fls_index,value};assert(invoke(&cpu,4,2,args));
    assert(invoke(&cpu,2,1,&fls_index)&&callbacks==2);assert(!tribes_global_size(cpu._pointer_offset,value));
    /* Guest window intentionally stays mapped through main-thread FLS teardown. */
    puts("PASS: emitted ARM64 FLS stdcall wrappers and guest callbacks, native thread-exit owned cleanup stack, index-release cleanup, reentrant allocation/free, guest allocation disposal, GPR/flags and stack guards");return 0;
}
