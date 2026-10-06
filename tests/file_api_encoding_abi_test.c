#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include "../port/core/guest_modules.h"
#include "../port/core/guest_filesystem.h"
extern void tbimp_AreFileApisANSI(void),tbimp_GetModuleFileNameW(void),tbimp_WideCharToMultiByte(void),tbimp_CreateFileA(void),tbimp_ReadFile(void),tbimp_CloseHandle(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);

void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_AreFileApisANSI;if(address==2)return (void *)tbimp_GetModuleFileNameW;if(address==3)return (void *)tbimp_WideCharToMultiByte;if(address==4)return (void *)tbimp_CreateFileA;if(address==5)return (void *)tbimp_ReadFile;if(address==6)return (void *)tbimp_CloseHandle;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
int main(void) {
    char directory[]="/tmp/tribes-file-api-XXXXXX";assert(mkdtemp(directory));char filename[512];assert(snprintf(filename,sizeof filename,"%s/Tribes2.exe",directory)>0);
    const unsigned char payload[]={0x89,0x56,0,0xff,0x37,0x2a};int fd=open(filename,O_CREAT|O_EXCL|O_WRONLY,0600);assert(fd>=0&&write(fd,payload,sizeof payload)==sizeof payload&&!close(fd));
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));unsigned char *memory=window;uint64_t base=(uintptr_t)window;
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x10f003;
    const char guest_path[]="C:\\TribesPad\\Game\\Tribes2.exe";assert(tribes_module_register(base,0x400000,guest_path,1));assert(tribes_filesystem_configure(base,directory,directory,directory));
    for(unsigned i=0;i<200;i++) {
        tribes_set_last_error(123);uint32_t ansi=invoke(&cpu,1,0,NULL);assert(ansi&&tribes_get_last_error()==123);
        uint32_t path[3]={0,0x100801,261};assert(invoke(&cpu,2,3,path)==strlen(guest_path));
        uint32_t encode[8]={ansi?0:1,0,path[1],UINT32_MAX,0,0,0,0};uint32_t needed=invoke(&cpu,3,8,encode);assert(needed==strlen(guest_path)+1);
        encode[4]=0x100b01;encode[5]=needed;memset(memory+encode[4]-1,0x79,needed+2);assert(invoke(&cpu,3,8,encode)==needed&&!strcmp((char *)memory+encode[4],guest_path));assert(memory[encode[4]-1]==0x79&&memory[encode[4]+needed]==0x79&&tribes_get_last_error()==123);
        uint32_t open_args[7]={encode[4],0x80000000,1,0,3,0x80,0};uint32_t handle=invoke(&cpu,4,7,open_args);assert(handle&&handle!=UINT32_MAX);
        uint32_t read_args[5]={handle,0x100c01,sizeof payload,0x100d01,0};memset(memory+read_args[1]-1,0x79,sizeof payload+2);memset(memory+read_args[3]-1,0x79,6);
        assert(invoke(&cpu,5,5,read_args));uint32_t got;memcpy(&got,memory+read_args[3],4);assert(got==sizeof payload&&!memcmp(memory+read_args[1],payload,sizeof payload));assert(memory[read_args[1]-1]==0x79&&memory[read_args[1]+sizeof payload]==0x79&&memory[read_args[3]-1]==0x79&&memory[read_args[3]+4]==0x79);
        assert(invoke(&cpu,6,1,&handle));
    }
    assert(!unlink(filename)&&!rmdir(directory)&&!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 200 actual ARM64 zero-argument file-mode / wide module filename / eight-argument CP selector conversion / seven-argument real file open / read / close cycles, native mounted payload bytes, NUL and neighboring guards, CPU/flags/stack and initial mode LastError; ASCII filename/default ANSI profile");return 0;
}
