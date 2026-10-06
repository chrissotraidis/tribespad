#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include "../port/core/guest_modules.h"
extern void tbimp_GetModuleFileNameW(void),tbimp_GetModuleFileNameA(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);

void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_GetModuleFileNameW;if(address==2)return (void *)tbimp_GetModuleFileNameA;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
static const char main_path[]="C:\\TribesPad\\Game\\Tribes2.exe";
static void check(const unsigned char *memory,uint32_t output,const char *text,uint32_t units,unsigned width) {
    for(uint32_t i=0;i<units;i++){uint16_t unit;unit=0;memcpy(&unit,memory+output+width*i,width);assert(unit==(unsigned char)text[i]);}
    uint16_t zero=0;memcpy(&zero,memory+output+width*units,width);assert(!zero);
}
struct Worker { uint64_t base;uint32_t output,stack; };
static void *worker(void *context) {
    struct Worker *w=context;unsigned char *memory=(void *)(uintptr_t)w->base;uint32_t n=(uint32_t)strlen(main_path);
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=w->base;cpu._esp=w->stack;
    for(unsigned i=0;i<1000;i++)for(unsigned target=1;target<=2;target++) {
        unsigned width=target==1?2:1;uint32_t args[3]={0,w->output,n+1};tribes_set_last_error(123);memset(memory+w->output-1,0x79,80);
        assert(invoke(&cpu,target,3,args)==n&&tribes_get_last_error()==123);check(memory,w->output,main_path,n,width);assert(memory[w->output-1]==0x79&&memory[w->output+width*(n+1)]==0x79);
    }
    return NULL;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x90000,PROT_READ|PROT_WRITE));unsigned char *memory=window;uint64_t base=(uintptr_t)window;
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x10f003;
    assert(tribes_module_register(base,0x400000,main_path,1));assert(tribes_module_register(base,0x10000000,"C:\\TribesPad\\Game\\IFC22.dll",0));
    TribesNativeExport exports[]={{"Example",0,17}};const char provider_path[]="C:\\Windows\\System32\\kernel32.dll";assert(tribes_module_define_native(base,0x60010000,provider_path,exports,1));
    uint32_t n=(uint32_t)strlen(main_path),output=0x100801;
    for(unsigned target=1;target<=2;target++){
    unsigned width=target==1?2:1;
    for(unsigned i=0;i<1000;i++) {
        uint32_t capacity=i%5==0?1:i%5==1?n:i%5==2?n+1:i%5==3?261:0;
        uint32_t args[3]={i%2?0x400000:0,output,capacity};memset(memory+output-1,0x79,524);tribes_set_last_error(123);
        uint32_t result=invoke(&cpu,target,3,args),copied=capacity?(capacity<=n?capacity-1:n):0;
        assert(result==(capacity<=n?capacity:n)&&tribes_get_last_error()==(capacity<=n?122:123));
        if(capacity)check(memory,output,main_path,copied,width);
        assert(memory[output-1]==0x79);for(unsigned j=capacity?width*(copied+1):0;j<523;j++)assert(memory[output+j]==0x79);
    }
    uint32_t args[3]={0x10000000,output,261};assert(invoke(&cpu,target,3,args)==strlen("C:\\TribesPad\\Game\\IFC22.dll"));check(memory,output,"C:\\TribesPad\\Game\\IFC22.dll",(uint32_t)strlen("C:\\TribesPad\\Game\\IFC22.dll"),width);
    args[0]=0x123456;memset(memory+output,0x79,80);assert(!invoke(&cpu,target,3,args)&&tribes_get_last_error()==126);for(unsigned i=0;i<80;i++)assert(memory[output+i]==0x79);
    args[0]=0x60010000;assert(!invoke(&cpu,target,3,args)&&tribes_get_last_error()==126);strcpy((char *)memory+0x100901,"kernel32");assert(tribes_load_library(base,0x100901)==0x60010000);
    tribes_set_last_error(123);assert(invoke(&cpu,target,3,args)==strlen(provider_path)&&tribes_get_last_error()==123);check(memory,output,provider_path,(uint32_t)strlen(provider_path),width);assert(tribes_free_library(base,0x60010000));assert(!invoke(&cpu,target,3,args)&&tribes_get_last_error()==126);
    args[0]=0;args[1]=0xfffffffe;args[2]=3;assert(!invoke(&cpu,target,3,args)&&tribes_get_last_error()==87);args[1]=output;args[2]=UINT32_MAX;assert(!invoke(&cpu,target,3,args)&&tribes_get_last_error()==87);
    args[1]=0;args[2]=261;assert(!invoke(&cpu,target,3,args)&&tribes_get_last_error()==87);
    }
    pthread_t threads[8];struct Worker workers[8];for(unsigned i=0;i<8;i++){workers[i]=(struct Worker){base,0x101001+4096*i,0x11f003+65536*i};assert(!pthread_create(&threads[i],NULL,worker,&workers[i]));}for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 2000 actual ARM64 three-argument ANSI/UTF-16 main-module path calls, capacity 0/1/exact/short/261, odd pointers and output/CPU/flags/stack guards, loaded/unloaded provider and image identity, range errors, plus 16000 concurrent actual ARM64 filename calls; modern truncation / ASCII metadata profile");return 0;
}
