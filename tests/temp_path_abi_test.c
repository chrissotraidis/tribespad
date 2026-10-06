#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/process_environment.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
extern void tribes_enter(_cpu *,uint32_t);
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
int main(int argc,char **argv) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));unsigned char *memory=window;
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    assert(tribes_memory_checks_configure(cpu._pointer_offset,UINT64_C(0x100000000)));
#ifdef CHECKED
    tribes_memory_external_configure(cpu._pointer_offset,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
    uint32_t unconfigured[]={0x100801,260};assert(!invoke(&cpu,5,2,unconfigured)&&tribes_get_last_error()==3);
    const char env[]="TMP=C:\\TribesPad\\Temp\0TEMP=C:\\Wrong\0";const char *expected="C:\\TribesPad\\Temp\\";uint32_t length=(uint32_t)strlen(expected),output=0x100801;
    assert(tribes_environment_configure(cpu._pointer_offset,env,sizeof env)&&tribes_temp_directory_configure(cpu._pointer_offset,"C:\\Windows"));
    installation_queries(&cpu,output,"C:\\Windows","C:\\Windows\\System32");
    char root[]="/tmp/tribespad-font-folder-XXXXXX";assert(mkdtemp(root));char host[1024];
    const char *directories[]={"Game","Temp","Windows"};
    for(unsigned i=0;i<3;i++){snprintf(host,sizeof host,"%s/%s",root,directories[i]);assert(!mkdir(host,0700));}
    char game[1024],temp[1024];snprintf(game,sizeof game,"%s/Game",root);snprintf(temp,sizeof temp,"%s/Temp",root);
    assert(tribes_filesystem_configure(cpu._pointer_offset,root,game,temp));
    uint32_t folder_args[]={0,0x14,0,0,output};memset(memory+output-1,0x79,260);
    tribes_set_last_error(123);assert(invoke(&cpu,7,5,folder_args)==UINT32_C(0x80070003)&&tribes_get_last_error()==123);
    assert(!memory[output]&&memory[output-1]==0x79&&memory[output+1]==0x79);
    snprintf(host,sizeof host,"%s/Windows/FoNtS",root);assert(!mkdir(host,0700));
    memset(memory+output-1,0x79,260);assert(!invoke(&cpu,7,5,folder_args)&&tribes_get_last_error()==123);
    assert(!strcmp((char *)memory+output,"C:\\Windows\\Fonts")&&memory[output-1]==0x79&&memory[output+17]==0x79);
    assert(tribes_temp_directory_configure(cpu._pointer_offset,"C:\\Windows\\"));assert(!invoke(&cpu,7,5,folder_args)&&!strcmp((char *)memory+output,"C:\\Windows\\Fonts"));
    if(argc==2){
        if(!strcmp(argv[1],"unsupported-folder"))folder_args[1]=0x1a;
        else if(!strcmp(argv[1],"foreign-token"))folder_args[2]=1;
        else if(!strcmp(argv[1],"readonly-folder")){assert(tribes_memory_permissions_set(0x100000,0x1000,1));}
        else if(!strcmp(argv[1],"cross-page-folder")){folder_args[4]=0x100ffc;assert(tribes_memory_permissions_set(0x101000,0x1000,0));}
        else abort();
        invoke(&cpu,7,5,folder_args);return 99;
    }
    /* Real directory removal changes the result; outside-mount links cannot
       become shell paths, and failure clears only the first output byte. */
    assert(!rmdir(host));assert(!symlink("/System/Library/Fonts",host));
    memset(memory+output-1,0x79,260);assert(invoke(&cpu,7,5,folder_args)==UINT32_C(0x80070005));
    assert(!memory[output]&&memory[output-1]==0x79&&memory[output+1]==0x79);assert(!unlink(host));
    FILE *file=fopen(host,"w");assert(file);assert(!fclose(file));assert(invoke(&cpu,7,5,folder_args)==UINT32_C(0x80070003));assert(!unlink(host));
    folder_args[4]=0;assert(invoke(&cpu,7,5,folder_args)==UINT32_C(0x80070057));folder_args[4]=UINT32_MAX;assert(invoke(&cpu,7,5,folder_args)==UINT32_C(0x80070057));
    for(unsigned i=0;i<3;i++){snprintf(host,sizeof host,"%s/%s",root,directories[i]);assert(!rmdir(host));}assert(!rmdir(root));

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
    puts("PASS: actual ARM64 shell fonts folder: real mounted existence/missing/file/escaped-link, exact path/empty-error output and stdcall CPU/LastError; Windows/System directory wrappers: configured paths/root/trailing separator/max length, size/short/exact/large capacities, invalid pointers and LastError; 3000 temporary path calls plus 1000 QPC/frequency pairs; full CPU/flags/stack and output-neighbor preservation");return 0;
}
