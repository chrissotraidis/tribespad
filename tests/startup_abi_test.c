#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/process_startup.h"
#include "../port/core/process_environment.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tbimp_SetHandleCount(void);
extern void tbimp_GetStartupInfoA(void),tbimp_GetStartupInfoW(void),tbimp_GetCommandLineA(void),tbimp_GetCommandLineW(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t address) {
    switch(address){case 1:return (void *)tbimp_GetStartupInfoA;case 2:return (void *)tbimp_GetStartupInfoW;case 3:return (void *)tbimp_GetCommandLineA;case 4:return (void *)tbimp_GetCommandLineW;case 5:return (void *)tbimp_SetHandleCount;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}
}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t word(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static void invoke(_cpu *cpu,uint32_t target,uint32_t output) {
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;uint32_t top=cpu->_esp;
    memset(memory+top-72,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp-=4;memcpy(memory+cpu->_esp,&output,4);_cpu before=*cpu;
    tribes_enter(cpu,target);before._esp=top;if(target==5)before._eax=output;assert(!memcmp(&before,cpu,sizeof before));
    assert(word(memory+top-4)==output);for(unsigned i=0;i<64;i++)assert(memory[top-72+i]==0xa5&&memory[top+i]==0xa5);
}
static uint32_t command(_cpu *cpu,uint32_t target) {
    unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;uint32_t top=cpu->_esp;
    memset(memory+top-68,0xa5,64);memset(memory+top,0xa5,64);_cpu before=*cpu;
    tribes_enter(cpu,target);before._eax=cpu->_eax;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[top-68+i]==0xa5&&memory[top+i]==0xa5);
    return cpu->_eax;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);assert(!mprotect((char *)window+0x100000,0x20000,PROT_READ|PROT_WRITE));
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    uint32_t initial[3];for(unsigned i=0;i<3;i++)initial[i]=tribes_get_std_handle(0xfffffff6-i);
    assert(tribes_startup_configure(cpu._pointer_offset,1,initial));assert(tribes_set_std_handle(0xfffffff5,0xdeadbeef));
    const unsigned char narrow[]={0x22,0xe9,0x22,0};const unsigned char wide[]={0x22,0,0xe9,0,0x22,0,0,0};
    assert(tribes_command_line_configure(cpu._pointer_offset,narrow,sizeof narrow));assert(tribes_command_line_configure_w(cpu._pointer_offset,wide,sizeof wide));
    uint32_t a=0,w=0;
    for(unsigned i=0;i<1000;i++) {
        tribes_set_last_error(123);uint32_t next_a=command(&cpu,3),next_w=command(&cpu,4);
        assert(next_a&&next_w&&next_a!=next_w&&(!a||a==next_a)&&(!w||w==next_w));a=next_a;w=next_w;
        assert(!memcmp((char *)window+a,narrow,sizeof narrow)&&!memcmp((char *)window+w,wide,sizeof wide)&&tribes_get_last_error()==123);
    }
    unsigned char expected[68]={0};uint32_t size=68,flags=0x101;memcpy(expected,&size,4);memcpy(expected+44,&flags,4);expected[48]=1;memcpy(expected+56,initial,12);
    for(unsigned i=0;i<1000;i++) {
        uint32_t output=0x110001+(i%3);unsigned char *bytes=(unsigned char *)window+output;
        memset(bytes-16,0x79,100);tribes_set_last_error(123);invoke(&cpu,1+i%2,output);
        assert(!memcmp(bytes,expected,68)&&tribes_get_last_error()==123);for(unsigned j=0;j<16;j++)assert(bytes[(int)j-16]==0x79&&bytes[68+j]==0x79);
    }
    uint32_t counts[]={0,1,20,32,256,65535,UINT32_MAX};
    for(unsigned i=0;i<sizeof counts/sizeof *counts;i++){tribes_set_last_error(123);invoke(&cpu,5,counts[i]);assert(cpu._eax==counts[i]&&tribes_get_last_error()==123&&tribes_get_std_handle(0xfffffff5)==0xdeadbeef);}
    assert(tribes_set_std_handle(0xfffffff5,initial[1]));assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: emitted ARM64 ANSI/wide startup-info stdcall wrappers, 1000 unaligned guarded outputs, immutable captured native handles, NULL startup strings, 2000 ANSI/UTF-16 command-line calls with stable pointers and non-ASCII bytes, NT SetHandleCount identity/unchanged registry, LastError and complete CPU/stack preservation");return 0;
}
