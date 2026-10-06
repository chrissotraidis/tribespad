#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/process_environment.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tbimp_GetEnvironmentVariableA(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_GetEnvironmentVariableA;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
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
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));unsigned char *memory=window;
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    const char env[]="mimalloc_verbose=1\0EMPTY=\0";assert(tribes_environment_configure(cpu._pointer_offset,env,sizeof env));
    uint32_t name=0x100801,output=0x100901;
    for(unsigned i=0;i<1000;i++) {
        strcpy((char *)memory+name,"MiMaLlOc_VeRbOsE");uint32_t args[]={name,UINT32_MAX,0};tribes_set_last_error(123);
        assert(invoke(&cpu,1,3,args)==2&&tribes_get_last_error()==123);
        memset(memory+output-1,0x79,8);args[1]=output;args[2]=1;
        assert(invoke(&cpu,1,3,args)==2&&tribes_get_last_error()==123);
        for(unsigned n=0;n<8;n++)assert(memory[output-1+n]==0x79);
        args[2]=2;assert(invoke(&cpu,1,3,args)==1&&tribes_get_last_error()==123);
        assert(memory[output]=='1'&&!memory[output+1]&&memory[output-1]==0x79&&memory[output+2]==0x79&&!strcmp((char *)memory+name,"MiMaLlOc_VeRbOsE"));
        strcpy((char *)memory+name,"mimalloc_show_errors");args[2]=65;memset(memory+output-1,0x79,68);
        assert(!invoke(&cpu,1,3,args)&&tribes_get_last_error()==203);
        for(unsigned n=0;n<68;n++)assert(memory[output-1+n]==0x79);
        strcpy((char *)memory+name,"EMPTY");args[2]=1;tribes_set_last_error(123);
        assert(!invoke(&cpu,1,3,args)&&!memory[output]&&tribes_get_last_error()==123&&memory[output-1]==0x79&&memory[output+1]==0x79);
    }
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 5000 actual ARM64 three-argument environment size/short/exact/missing/empty calls, case-independent ASCII names, explicit guest values, absent mimalloc setting error 203, CPU/flags/stack/source/output/LastError guards");return 0;
}
