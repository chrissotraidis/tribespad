#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tbimp_InitializeSListHead(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t address) {
    if(address==1)return (void *)tbimp_InitializeSListHead;
    if(address==0xfffff000)return (void *)tribes_return_to_host;
    abort();
}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    unsigned char *memory=window;
    for(unsigned i=0;i<1000;i++) {
        /* Alternate 16-byte and eight-only alignment to distinguish x86/Win64. */
        uint32_t head=0x100010+(i%32)*8;memset(memory+head-8,0x79,32);memset(memory+head,(i%254)+1,8);
        cpu._eax=i*73+17;tribes_set_last_error(123);uint32_t before=cpu._eax;
        assert(invoke(&cpu,1,1,&head)==before&&tribes_get_last_error()==123);
        for(unsigned n=0;n<8;n++)assert(memory[head+n]==0);
        for(unsigned n=0;n<8;n++)assert(memory[head-8+n]==0x79);
        for(unsigned n=8;n<24;n++)assert(memory[head+n]==0x79);
    }
    /* Independent guest image: no host/global list registry or native pointers. */
    void *other=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(other!=MAP_FAILED);
    assert(!mprotect((char *)other+0x100000,0x10000,PROT_READ|PROT_WRITE));
    memset((char *)other+0x100010,0x81,8);memset(memory+0x100010,0x73,8);cpu._pointer_offset=(uintptr_t)other;uint32_t head=0x100010;
    assert(invoke(&cpu,1,1,&head)==cpu._eax);
    for(unsigned i=0;i<8;i++)assert(!*((unsigned char *)other+head+i)&&memory[head+i]==0x73);
    assert(!munmap(other,UINT64_C(0x100000000))&&!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 1001 actual ARM64 void stdcall SLIST initializations, eight-byte x86 layout/alignment, neighboring memory/LastError/CPU/EAX/flags/stack preservation and independent guest images; push/pop remain unsupported");return 0;
}
