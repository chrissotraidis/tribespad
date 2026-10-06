#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_virtual_memory.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t);
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
int main(int argc,char **argv) {
    (void)argv;
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));
#ifdef CHECKED_TEST
    uint64_t base=(uintptr_t)window;
    assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));
    const TribesVirtualMemoryBackend backend={tribes_memory_vm_initialize,tribes_memory_vm_change};
    const void *targets[]={(void *)tribes_memory_vm_initialize,(void *)tribes_memory_vm_change};
    tribes_memory_register_targets(targets,2);tribes_memory_external_enable();assert(tribes_virtual_memory_enable_checked(base,&backend));
#endif
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    tribes_set_last_error(123);uint32_t heap=invoke(&cpu,1,0,NULL);assert(heap==invoke(&cpu,1,0,NULL)&&tribes_get_last_error()==123);
    for(unsigned i=0;i<1000;i++) {
        uint32_t args[]={heap,8,31+i%257};uint32_t pointer=invoke(&cpu,2,3,args);assert(pointer&&!(pointer&7));
        unsigned char *bytes=(unsigned char *)window+pointer;for(unsigned j=0;j<args[2];j++)assert(!bytes[j]);memset(bytes,0x79,args[2]);
        args[1]=0;args[2]=pointer;assert(invoke(&cpu,3,3,args)==1&&tribes_get_last_error()==123);
    }
    uint32_t overflow[]={heap,0,UINT32_MAX};assert(!invoke(&cpu,2,3,overflow)&&tribes_get_last_error()==123);
    uint32_t create[]={1,4096,0},h=invoke(&cpu,4,3,create),other=invoke(&cpu,4,3,create);
    assert(h&&other&&h!=other&&h!=heap&&invoke(&cpu,1,0,NULL)==heap);
    uint32_t alloc[]={h,9,13},a=invoke(&cpu,2,3,alloc);assert(a>=0x60030000&&!(a&7));
    uint32_t fields[7];assert(tribes_virtual_memory_query((uintptr_t)window,a&~4095u,fields)&&fields[4]==0x1000&&fields[5]==4);
    unsigned char *bytes=(unsigned char *)window+a;memset(bytes,0x79,16);
    uint32_t resize[]={h,24,a,31};assert(invoke(&cpu,6,4,resize)==a);
    for(unsigned i=0;i<13;i++)assert(bytes[i]==0x79);for(unsigned i=13;i<31;i++)assert(bytes[i]==0);
    /* An adjacent live block forces in-place failure, with contents retained. */
    alloc[2]=32;uint32_t blocker=invoke(&cpu,2,3,alloc);assert(blocker==a+32);
    resize[3]=128;assert(!invoke(&cpu,6,4,resize));for(unsigned i=0;i<13;i++)assert(bytes[i]==0x79);
    resize[1]=8;uint32_t moved=invoke(&cpu,6,4,resize);assert(moved&&moved!=a);bytes=(unsigned char *)window+moved;
    for(unsigned i=0;i<13;i++)assert(bytes[i]==0x79);for(unsigned i=13;i<128;i++)assert(!bytes[i]);
    uint32_t release[]={other,1,moved};assert(!invoke(&cpu,3,3,release));
    resize[0]=other;resize[2]=moved;assert(!invoke(&cpu,6,4,resize));
    resize[0]=h;resize[3]=UINT32_MAX;assert(!invoke(&cpu,6,4,resize));assert(bytes[0]==0x79);
    resize[3]=9;assert(invoke(&cpu,6,4,resize)==moved);memset(bytes+9,0x66,7);
    resize[3]=16;assert(invoke(&cpu,6,4,resize)==moved);for(unsigned i=9;i<16;i++)assert(!bytes[i]);
    /* Beyond the first reservation, plus a second heap that must survive destroy. */
    alloc[2]=0x200123;uint32_t large=invoke(&cpu,2,3,alloc);assert(large);((unsigned char *)window)[large+0x200122]=0x55;
    alloc[0]=other;alloc[2]=8;uint32_t survivor=invoke(&cpu,2,3,alloc);assert(survivor);((unsigned char *)window)[survivor]=0x44;
    tribes_set_last_error(123);assert(invoke(&cpu,5,1,&h)&&tribes_get_last_error()==123);
    assert(tribes_virtual_memory_query((uintptr_t)window,large&~4095u,fields)&&fields[4]==0x10000);
    if(argc>1){
#ifdef CHECKED_TEST
        tribes_memory_access_begin((unsigned char *)window+large,1,1);
#else
        volatile unsigned char stale=((unsigned char *)window)[large];(void)stale;
#endif
        abort();
    }
    assert(((unsigned char *)window)[survivor]==0x44);assert(!invoke(&cpu,5,1,&h));assert(!invoke(&cpu,5,1,&heap));
    alloc[0]=h;assert(!invoke(&cpu,2,3,alloc));assert(invoke(&cpu,5,1,&other));
    for(unsigned i=0;i<100;i++){h=invoke(&cpu,4,3,create);assert(h);alloc[0]=h;assert(invoke(&cpu,2,3,alloc));assert(invoke(&cpu,5,1,&h));}
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: emitted ARM64 fastcc process-heap stdcall wrappers, 1000 allocations/free calls, private ownership/growth/realloc/destroy, 100 lifetimes, unaligned frames, complete CPU preservation, stack/argument guards and OOM return");return 0;
}
