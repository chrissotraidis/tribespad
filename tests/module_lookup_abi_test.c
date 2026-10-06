#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_modules.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
extern void tbimp_GetModuleHandleW(void),tbimp_GetProcAddress(void),tbimp_GetSystemTimePreciseAsFileTime(void),tbimp_LoadLibraryExW(void),tbimp_FreeLibrary(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);
extern void tbimp_LoadLibraryExA(void);
void *tribes_lookup(uint32_t address) {
    switch(address){case 1:return (void *)tbimp_GetModuleHandleW;case 2:return (void *)tbimp_GetSystemTimePreciseAsFileTime;case 3:return (void *)tbimp_GetProcAddress;case 4:return (void *)tbimp_LoadLibraryExW;case 5:return (void *)tbimp_FreeLibrary;case 6:return (void *)tbimp_LoadLibraryExA;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}
}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint64_t wall(void){struct timespec t;assert(!clock_gettime(CLOCK_REALTIME,&t));return ((uint64_t)t.tv_sec+11644473600)*10000000+(uint64_t)t.tv_nsec/100;}
static void wide(unsigned char *memory,uint32_t address,const char *text){for(unsigned i=0;;i++){uint16_t ch=(unsigned char)text[i];memcpy(memory+address+2*i,&ch,2);if(!ch)break;}}
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
    uint64_t base=(uintptr_t)window;_cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x10f003;
    assert(tribes_module_register(base,0x400000,"C:\\TribesPad\\Game\\Tribes2.exe",1));
    TribesNativeExport exports[]={{"GetSystemTimePreciseAsFileTime",0,2}};
    assert(tribes_module_define_native(base,0x60010000,"C:\\Windows\\System32\\kernel32.dll",exports,1));
    uint32_t name=0x100801,symbol=0x100901,output=0x100a01;wide(memory,name,"KeRnEl32");strcpy((char *)memory+symbol,"GetSystemTimePreciseAsFileTime");
    uint32_t ansi_name=0x100b01;strcpy((char *)memory+ansi_name,"KeRnEl32");
    uint32_t rejected[][3]={{0,0,0},{ansi_name,1,0},{UINT32_MAX,0,0},{0xdead0000,0,0x800}};
    for(unsigned i=0;i<sizeof rejected/sizeof rejected[0];i++)assert(!invoke(&cpu,6,3,rejected[i])&&tribes_get_last_error()==87);
    for(unsigned i=0;i<1000;i++) {
        uint32_t zero=0;tribes_set_last_error(123);assert(invoke(&cpu,1,1,&zero)==0x400000&&tribes_get_last_error()==123);
        assert(!invoke(&cpu,1,1,&name)&&tribes_get_last_error()==126);
        uint32_t load[]={i&1?ansi_name:name,0,0};tribes_set_last_error(123);assert(invoke(&cpu,i&1?6:4,3,load)==0x60010000&&tribes_get_last_error()==123);
        assert(invoke(&cpu,1,1,&name)==0x60010000&&tribes_get_last_error()==123);
        uint32_t lookup[]={0x60010000,symbol};uint32_t target=invoke(&cpu,3,2,lookup);assert(target==2);
        memset(memory+output-1,0x79,10);cpu._eax=i*71+17;uint32_t saved_eax=cpu._eax;uint64_t before=wall();
        assert(invoke(&cpu,target,1,&output)==saved_eax&&tribes_get_last_error()==123);
        uint64_t after=wall(),sample;memcpy(&sample,memory+output,8);assert(before<=sample&&sample<=after&&memory[output-1]==0x79&&memory[output+8]==0x79);
        uint32_t module=0x60010000;assert(invoke(&cpu,5,1,&module)==1);
        /* If GetModuleHandleW added a reference, this lookup would still succeed. */
        assert(!invoke(&cpu,1,1,&name)&&tribes_get_last_error()==126);
    }
    wide(memory,name,"C:\\TRIBESPAD\\GAME\\TRIBES2.EXE");assert(invoke(&cpu,1,1,&name)==0x400000);
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 1000 emitted ARM64 ANSI/wide provider load/lookup/export/clock/free cycles, rejected ANSI parameters/search mode, no added module reference, UTC FILETIME host bounds, odd UTF-16/output addresses, CPU/flags/EAX/guest stack/neighbor/LastError preservation and full-path lookup");return 0;
}
