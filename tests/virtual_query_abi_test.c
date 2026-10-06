#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_address_space.h"
#include "../port/core/guest_virtual_memory.h"
#include "../port/core/process_heap.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
extern void tbimp_VirtualQuery(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){if(pc==1)return (void *)tbimp_VirtualQuery;if(pc==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static unsigned char *memory;
static uint64_t base;
static uint32_t call(_cpu *cpu,uint32_t address,uint32_t output,uint32_t length){
    uint32_t args[]={address,output,length};uint32_t top=cpu->_esp,sp=top-12;memcpy(memory+sp,args,12);memset(memory+sp-68,0x79,64);memset(memory+top,0x79,64);cpu->_esp=sp;_cpu before=*cpu;
    tribes_enter(cpu,1);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));assert(!memcmp(memory+sp,args,12));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x79&&memory[top+i]==0x79);return result;
}
static void check(_cpu *cpu,uint32_t address,const uint32_t expected[7]){
    uint32_t output=cpu->_esp-0x601;memset(memory+output-1,0x79,34);tribes_set_last_error(123);
    assert(call(cpu,address,output,31)==28&&!memcmp(memory+output,expected,28));assert(memory[output-1]==0x79);
    for(unsigned i=28;i<33;i++)assert(memory[output+i]==0x79);assert(tribes_get_last_error()==123);
}
static void setup(_cpu *cpu,unsigned i){memset(cpu,0xa5,sizeof *cpu);cpu->_pointer_offset=base;cpu->_esp=0x100000+i*65536+0xf003;}
static void *worker(void *context){
    _cpu cpu;setup(&cpu,(unsigned)(uintptr_t)context);long page=sysconf(_SC_PAGESIZE);
    for(unsigned i=0;i<100;i++){
        uint32_t address=tribes_virtual_alloc(base,0,2*(uint32_t)page,0x2000,4);assert(address);
        uint32_t reserved[]={address,address,4,2*(uint32_t)page,0x2000,0,0x20000};check(&cpu,address+1,reserved);
        assert(tribes_virtual_alloc(base,address,(uint32_t)page,0x1000,2)==address);uint32_t committed[]={address,address,4,(uint32_t)page,0x1000,2,0x20000};check(&cpu,address+7,committed);
        reserved[0]=address+(uint32_t)page;reserved[3]=(uint32_t)page;check(&cpu,reserved[0]+123,reserved);
        assert(tribes_virtual_free(base,address,(uint32_t)page,0x4000));reserved[0]=address;reserved[3]=2*(uint32_t)page;check(&cpu,address,reserved);
        assert(tribes_virtual_free(base,address,0,0x8000));
    }return NULL;
}
int main(int argc,char **argv){
    (void)argv;memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(!mprotect(memory+0x100000,0x80000,PROT_READ|PROT_WRITE));_cpu cpu;setup(&cpu,0);
    if(argc>1){(void)call(&cpu,0x400000,cpu._esp-0x601,28);return 99;}
    assert(tribes_address_space_begin(base));assert(tribes_address_space_declare(base,0x100000,0x80000,0x100000,4,0x1000,4,0x20000));
    assert(tribes_address_space_declare(base,0x400000,0x9000,0x400000,0x80,0x1000,1,0x1000000));
    assert(tribes_address_space_protect(base,0x400000,0x1000,2));assert(tribes_address_space_protect(base,0x401000,0x4000,0x20));assert(tribes_address_space_protect(base,0x405000,0x2000,2));
    assert(tribes_address_space_declare(base,0x409000,0x1000,0x409000,0x80,0x1000,1,0x1000000));
    assert(!tribes_address_space_declare(base,0x400000,0x1000,0x400000,0x80,0x1000,4,0x1000000)&&tribes_get_last_error()==487);
    assert(!tribes_address_space_protect(base,0x408000,0x2000,4)&&tribes_get_last_error()==487);
    assert(!tribes_address_space_declare(base,0x60030000,0x1000,0x60030000,4,0x2000,0,0x20000));assert(tribes_address_space_seal(base));
    assert(!tribes_address_space_declare(base,0x40a000,0x1000,0x40a000,4,0x1000,4,0x20000));assert(!tribes_address_space_begin(base));
    uint32_t header[]={0x400000,0x400000,0x80,4096,0x1000,2,0x1000000};check(&cpu,0x400abc,header);
    uint32_t text[]={0x403000,0x400000,0x80,8192,0x1000,0x20,0x1000000};check(&cpu,0x403abc,text);
    assert(tribes_address_space_protect(base,0x403000,0x1000,0x40));text[3]=4096;text[5]=0x40;check(&cpu,0x403123,text);
    uint32_t separate[]={0x408000,0x400000,0x80,4096,0x1000,1,0x1000000};check(&cpu,0x408999,separate);separate[0]=separate[1]=0x409000;check(&cpu,0x409123,separate);
    uint32_t freegap[]={0x40a000,0,0,0x30000000-0x40a000,0x10000,0,0};check(&cpu,0x40a777,freegap);
    uint32_t output=cpu._esp-0x601;memset(memory+output,0x79,28);assert(!call(&cpu,0x80000000,output,28)&&tribes_get_last_error()==87);
    assert(!call(&cpu,0x400000,output,27)&&tribes_get_last_error()==24);for(unsigned i=0;i<28;i++)assert(memory[output+i]==0x79);
    assert(!call(&cpu,0x400000,0,28)&&tribes_get_last_error()==998);assert(!call(&cpu,0x400000,0x400011,28)&&tribes_get_last_error()==998);
    uint32_t heap=tribes_get_process_heap(base);assert(heap);uint32_t p=tribes_heap_alloc(base,heap,8,37);assert(p==0x50000000);long native_page=sysconf(_SC_PAGESIZE);
    uint32_t process[]={p,0x50000000,4,(uint32_t)native_page,0x1000,4,0x20000};check(&cpu,p+3,process);assert(tribes_heap_free(base,heap,0,p));check(&cpu,p+3,process);
    uint32_t g=tribes_global_alloc(base,0,37);assert(g==0x30000000);uint32_t global[]={g,0x30000000,4,(uint32_t)native_page,0x1000,4,0x20000};check(&cpu,g+8,global);assert(!tribes_global_free(base,g));global[3]=0x20000000;global[4]=0x2000;global[5]=0;check(&cpu,g+8,global);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    uint32_t allfree[]={0x60030000,0,0,0x7fff0000-0x60030000,0x10000,0,0};check(&cpu,0x60030000,allfree);
    assert(!munmap(memory,UINT64_C(0x100000000)));
    puts("PASS: actual ARM64 28-byte x86 VirtualQuery ABI, page-down/forward runs, image allocation identity and permission changes, protected outputs/guards/error cases, native GlobalAlloc release versus process-heap retained commit, 800 concurrent VM reserve/commit/decommit/release lifetimes and 3200 queries, complete caller CPU/flags/stack and LastError; logical loader profile, not native 4 KiB enforcement");return 0;
}
