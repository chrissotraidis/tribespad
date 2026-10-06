#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/process_environment.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
extern void tbimp_GetTempPathW(void),tbimp_GetTempPath2W(void),tbimp_QueryPerformanceCounter(void),tbimp_QueryPerformanceFrequency(void),tribes_return_to_host(void);
extern void tbimp_GetWindowsDirectoryA(void),tbimp_GetSystemDirectoryA(void);
extern void tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t address) {
    switch(address){case 1:return (void *)tbimp_GetTempPathW;case 2:return (void *)tbimp_GetTempPath2W;case 3:return (void *)tbimp_QueryPerformanceCounter;case 4:return (void *)tbimp_QueryPerformanceFrequency;case 5:return (void *)tbimp_GetWindowsDirectoryA;case 6:return (void *)tbimp_GetSystemDirectoryA;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}
}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint64_t monotonic(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
static void installation_queries(_cpu *cpu,uint32_t output,const char *windows,const char *system){
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    for(unsigned target=5;target<=6;target++){
        const char *want=target==5?windows:system;uint32_t length=(uint32_t)strlen(want),args[]={UINT32_MAX,0};
        tribes_set_last_error(123);assert(invoke(cpu,target,2,args)==length+1&&tribes_get_last_error()==123);
        memset(memory+output-1,0x79,512);args[0]=output;args[1]=length;
        assert(invoke(cpu,target,2,args)==length+1&&tribes_get_last_error()==123);
        for(unsigned i=0;i<512;i++)assert(memory[output-1+i]==0x79);
        args[1]=length+1;assert(invoke(cpu,target,2,args)==length&&!strcmp((char *)memory+output,want)&&tribes_get_last_error()==123);
        assert(memory[output-1]==0x79&&memory[output+length+1]==0x79);
        args[1]=UINT32_MAX;assert(invoke(cpu,target,2,args)==length&&tribes_get_last_error()==123);
        args[0]=0;assert(!invoke(cpu,target,2,args)&&tribes_get_last_error()==87);
        args[0]=UINT32_MAX;assert(!invoke(cpu,target,2,args)&&tribes_get_last_error()==87);
    }
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));unsigned char *memory=window;
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    uint32_t unconfigured[]={0x100801,260};assert(!invoke(&cpu,5,2,unconfigured)&&tribes_get_last_error()==3);
    const char env[]="TMP=C:\\TribesPad\\Temp\0TEMP=C:\\Wrong\0";const char *expected="C:\\TribesPad\\Temp\\";uint32_t length=(uint32_t)strlen(expected),output=0x100801;
    assert(tribes_environment_configure(cpu._pointer_offset,env,sizeof env)&&tribes_temp_directory_configure(cpu._pointer_offset,"C:\\Windows"));
    installation_queries(&cpu,output,"C:\\Windows","C:\\Windows\\System32");
    for(unsigned i=0;i<1000;i++) {
        uint32_t target=i%2+1,args[]={0,UINT32_MAX};tribes_set_last_error(123);assert(invoke(&cpu,target,2,args)==length+1&&tribes_get_last_error()==123);
        memset(memory+output-1,0x79,64);args[0]=length;args[1]=output;
        assert(invoke(&cpu,target,2,args)==length+1&&tribes_get_last_error()==123);
        for(unsigned n=0;n<64;n++)assert(memory[output-1+n]==0x79);
        args[0]=length+1;assert(invoke(&cpu,target,2,args)==length&&tribes_get_last_error()==123);
        for(unsigned n=0;n<=length;n++){uint16_t ch;memcpy(&ch,memory+output+2*n,2);assert(ch==(unsigned char)expected[n]);}
        assert(memory[output-1]==0x79&&memory[output+2*(length+1)]==0x79);
        memset(memory+output-1,0x79,10);assert(invoke(&cpu,4,1,&output)==1);uint64_t frequency;memcpy(&frequency,memory+output,8);assert(frequency==1000000000);
        uint64_t before=monotonic();assert(invoke(&cpu,3,1,&output)==1);uint64_t after=monotonic(),counter;memcpy(&counter,memory+output,8);
        assert(before<=counter&&counter<=after&&memory[output-1]==0x79&&memory[output+8]==0x79&&tribes_get_last_error()==123);
    }
    assert(tribes_temp_directory_configure(cpu._pointer_offset,"D:\\WINNT\\"));installation_queries(&cpu,output,"D:\\WINNT","D:\\WINNT\\System32");
    assert(tribes_temp_directory_configure(cpu._pointer_offset,"C:\\"));installation_queries(&cpu,output,"C:\\","C:\\System32");
    char longest[261],system[270];memcpy(longest,"C:\\",3);memset(longest+3,'x',257);longest[260]=0;snprintf(system,sizeof system,"%s\\System32",longest);
    assert(tribes_temp_directory_configure(cpu._pointer_offset,longest));installation_queries(&cpu,output,longest,system);
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: actual ARM64 Windows/System directory wrappers: configured paths/root/trailing separator/max length, size/short/exact/large capacities, invalid pointers and LastError; 3000 temporary path calls plus 1000 QPC/frequency pairs; full CPU/flags/stack and output-neighbor preservation");return 0;
}
