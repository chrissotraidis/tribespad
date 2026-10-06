#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include "../port/core/process_environment.h"
#include "environment_oem_expected.h"
extern void tbimp_GetEnvironmentStringsW(void),tbimp_FreeEnvironmentStringsW(void),tbimp_GetEnvironmentStrings(void),tbimp_FreeEnvironmentStringsA(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);

void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_GetEnvironmentStringsW;if(address==2)return (void *)tbimp_FreeEnvironmentStringsW;if(address==3)return (void *)tbimp_GetEnvironmentStrings;if(address==4)return (void *)tbimp_FreeEnvironmentStringsA;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
static unsigned char oem[259];
static void *worker(void *context) {
    uint64_t base=(uintptr_t)context;
    for(unsigned i=0;i<1000;i++) {
        tribes_set_last_error(123);uint32_t a=tribes_get_environment_strings(base),w=tribes_get_environment_strings_w(base);assert(a&&w&&a!=w&&tribes_get_last_error()==123);
        assert(!memcmp((void *)(uintptr_t)(base+a),oem,sizeof oem));assert(!memcmp((void *)(uintptr_t)(base+w),expected_oem_wide,sizeof expected_oem_wide));
        assert(tribes_environment_snapshot_size(base,w,1)==sizeof expected_oem_wide&&tribes_environment_snapshot_size(base,a,0)==sizeof oem);
        assert(tribes_free_environment_strings(base,a)&&tribes_free_environment_strings_w(base,w)&&tribes_get_last_error()==123);
    }return NULL;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));uint64_t base=(uintptr_t)window;
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x10f003;
    oem[0]='X';oem[1]='=';for(unsigned i=1;i<256;i++)oem[i+1]=(unsigned char)i;oem[257]=oem[258]=0;
    const char changed[]="EMPTY=\0Z=last\0";
    for(unsigned i=0;i<500;i++) {
        assert(tribes_environment_configure(base,oem,sizeof oem));tribes_set_last_error(123);
        uint32_t a=invoke(&cpu,3,0,NULL),w=invoke(&cpu,1,0,NULL),second=invoke(&cpu,1,0,NULL);assert(a&&w&&second&&a!=w&&w!=second&&a!=second&&tribes_get_last_error()==123);
        assert(!memcmp((void *)(uintptr_t)(base+a),oem,sizeof oem));assert(!memcmp((void *)(uintptr_t)(base+w),expected_oem_wide,sizeof expected_oem_wide));
        assert(tribes_environment_configure(base,changed,sizeof changed));assert(!memcmp((void *)(uintptr_t)(base+w),expected_oem_wide,sizeof expected_oem_wide));
        uint32_t fresh=invoke(&cpu,1,0,NULL);for(unsigned j=0;j<sizeof changed;j++){uint16_t unit;memcpy(&unit,(void *)(uintptr_t)(base+fresh+2*j),2);assert(unit==(unsigned char)changed[j]);}
        assert(!invoke(&cpu,2,1,&a)&&tribes_get_last_error()==87);assert(!invoke(&cpu,4,1,&w)&&tribes_get_last_error()==87);uint32_t interior=w+2;assert(!invoke(&cpu,2,1,&interior)&&tribes_get_last_error()==87);
        assert(tribes_environment_snapshot_size(base,w,1)==sizeof expected_oem_wide&&!tribes_environment_snapshot_size(base,w,0));
        tribes_set_last_error(123);assert(invoke(&cpu,2,1,&second)&&invoke(&cpu,4,1,&a)&&invoke(&cpu,2,1,&fresh)&&invoke(&cpu,2,1,&w)&&tribes_get_last_error()==123);
        assert(!invoke(&cpu,2,1,&w)&&tribes_get_last_error()==87&&!tribes_environment_snapshot_size(base,w,1));
    }
    const unsigned char empty[2]={0,0};assert(tribes_environment_configure(base,empty,2));uint32_t w=invoke(&cpu,1,0,NULL);assert(w&&tribes_environment_snapshot_size(base,w,1)==4);uint32_t zeros;memcpy(&zeros,(void *)(uintptr_t)(base+w),4);assert(!zeros&&invoke(&cpu,2,1,&w));
    assert(tribes_environment_configure(base,oem,sizeof oem));pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,window));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 500 actual ARM64 zero-/one-argument ANSI/wide owned snapshot lifetimes, all 256 OEM byte mappings via independent Python reference, double-NUL/empty values/block, reconfiguration retention, distinct allocations, wrong A/W/interior/double-free rejection, CPU/flags/stack/LastError, 16000 concurrent snapshots with matching frees");return 0;
}
