#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include "../port/core/guest_virtual_memory.h"
extern void tbimp_VirtualAlloc(void),tbimp_VirtualAlloc2FromApp(void),tbimp_VirtualFree(void),tbimp_NtAllocateVirtualMemoryEx(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);

void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_VirtualAlloc2FromApp;if(address==2)return (void *)tbimp_VirtualFree;if(address==3)return (void *)tbimp_NtAllocateVirtualMemoryEx;if(address==4)return (void *)tbimp_VirtualAlloc;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
static uint32_t allocate(_cpu *cpu,uint32_t address,uint32_t size,uint32_t type,uint32_t protection,uint32_t params) {
    uint32_t args[7]={UINT32_MAX,address,size,type,protection,params,params?1:0};return invoke(cpu,1,7,args);
}
static uint32_t classic(_cpu *cpu,uint32_t address,uint32_t size,uint32_t type,uint32_t protection) {
    uint32_t args[4]={address,size,type,protection};return invoke(cpu,4,4,args);
}
static uint32_t release(_cpu *cpu,uint32_t address,uint32_t size,uint32_t type) {
    uint32_t args[3]={address,size,type};return invoke(cpu,2,3,args);
}
struct Runs { unsigned count;uint32_t address[8],size[8],protection[8]; };
static int inspect(uint32_t address,uint32_t size,uint32_t protection,void *context) {
    struct Runs *runs=context;assert(runs->count<8);unsigned i=runs->count++;runs->address[i]=address;runs->size[i]=size;runs->protection[i]=protection;return 1;
}
static void *worker(void *context) {
    uint64_t base=(uintptr_t)context;
    for(unsigned i=0;i<500;i++) {
        tribes_set_last_error(123);uint32_t p=i%2?tribes_virtual_alloc(base,0,65536,0x3000,4):tribes_virtual_alloc2_from_app(base,0,0,65536,0x3000,4,0,0);assert(p&&tribes_get_last_error()==123);
        unsigned char *bytes=(void *)(uintptr_t)(base+p);for(unsigned j=0;j<65536;j++)assert(!bytes[j]);memset(bytes,0x73,65536);
        assert(tribes_virtual_free(base,p,0,0x8000)&&tribes_get_last_error()==123);
    }return NULL;
}
int main(int argc,char **argv) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));unsigned char *memory=window;uint64_t base=(uintptr_t)window;
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x10f003;
    if(argc>1) {
        if(!strcmp(argv[1],"original-reserved")) {
            uint32_t req[3]={0,0,0x400000};uint64_t param[2]={1,0x100901};memcpy(memory+0x100901,req,12);memcpy(memory+0x100801,param,16);
            uint32_t reserved=allocate(&cpu,0,0x8000000,0x2000,4,0x100801);assert(reserved&&!(reserved%0x400000));
            *(volatile unsigned char *)(memory+reserved)=92;return 99;
        }
        if(!strcmp(argv[1],"classic-executable")){(void)classic(&cpu,0,65536,0x3000,0x40);return 99;}
        if(!strcmp(argv[1],"classic-subpage")){(void)classic(&cpu,0,4096,0x3000,4);return 99;}
        uint32_t p=allocate(&cpu,0,65536,0x3000,4,0);assert(p);memory[p]=71;
        if(!strcmp(argv[1],"reserved")){assert(release(&cpu,p,0,0x4000));}
        else if(!strcmp(argv[1],"released")){assert(release(&cpu,p,0,0x8000));}
        else if(!strcmp(argv[1],"readonly")){assert(release(&cpu,p,0,0x8000));p=allocate(&cpu,0,65536,0x3000,2,0);assert(p);}
        else if(!strcmp(argv[1],"nt-executable")){uint32_t args[7]={UINT32_MAX,0x100801,0x100811,0x3000,0x40,0,0},zero=0,size=65536;memcpy(memory+args[1],&zero,4);memcpy(memory+args[2],&size,4);(void)invoke(&cpu,3,7,args);return 99;}
        else if(!strcmp(argv[1],"subpage")){(void)allocate(&cpu,0,4096,0x3000,4,0);return 99;}
        else abort();
        *(volatile unsigned char *)(memory+p)=92;return 99;
    }
    for(unsigned i=0;i<500;i++) {
        tribes_set_last_error(123);uint32_t p=allocate(&cpu,0,196608,0x2000,4,0);assert(p&&p%65536==0&&tribes_get_last_error()==123);
        struct Runs runs={0};assert(tribes_virtual_memory_visit(base,inspect,&runs));assert(runs.count==1&&runs.address[0]==p&&runs.size[0]==196608&&!runs.protection[0]);
        assert(allocate(&cpu,p,65536,0x1000,4,0)==p);for(unsigned j=0;j<65536;j++)assert(!memory[p+j]);memset(memory+p,0x73,65536);
        assert(allocate(&cpu,p,65536,0x1000,2,0)==p); /* recommit keeps old RW */memory[p+7]=88;assert(memory[p]==0x73);
        assert(allocate(&cpu,p+131072,65536,0x1000,4,0)==p+131072);memory[p+131072]=69;
        runs=(struct Runs){0};assert(tribes_virtual_memory_visit(base,inspect,&runs));assert(runs.count==3&&runs.protection[0]==4&&!runs.protection[1]&&runs.protection[2]==4);
        assert(release(&cpu,p,65536,0x4000));assert(release(&cpu,p,65536,0x4000));assert(allocate(&cpu,p,65536,0x1000,4,0)==p);for(unsigned j=0;j<65536;j++)assert(!memory[p+j]);assert(memory[p+131072]==69);
        assert(!release(&cpu,p+65536,0,0x8000)&&tribes_get_last_error()==487);
        assert(!release(&cpu,p,1,0x8000)&&tribes_get_last_error()==87);
        assert(release(&cpu,p,0,0x8000));assert(!release(&cpu,p,0,0x8000)&&tribes_get_last_error()==487);
        uint32_t again=allocate(&cpu,p,196608,0x3000,4,0);assert(again==p);for(unsigned j=0;j<196608;j++)assert(!memory[p+j]);assert(release(&cpu,p,0,0x8000));
    }
    /* Exact original QoL reserve request captured at 0x100b8dad. */
    {
        uint32_t req[3]={0,0,0x400000};uint64_t param[2]={1,0x100901};memcpy(memory+0x100901,req,12);memcpy(memory+0x100801,param,16);
        tribes_set_last_error(123);uint32_t reserved=allocate(&cpu,0,0x8000000,0x2000,4,0x100801);assert(reserved&&!(reserved%0x400000)&&tribes_get_last_error()==123);
        struct Runs runs={0};assert(tribes_virtual_memory_visit(base,inspect,&runs));assert(runs.count==1&&runs.address[0]==reserved&&runs.size[0]==0x8000000&&!runs.protection[0]);
        assert(classic(&cpu,reserved,0x400000,0x1000,4)==reserved);for(unsigned j=0;j<0x400000;j++)assert(!memory[reserved+j]);memory[reserved+0x3fffff]=92;
        runs=(struct Runs){0};assert(tribes_virtual_memory_visit(base,inspect,&runs));assert(runs.count==2&&runs.address[0]==reserved&&runs.size[0]==0x400000&&runs.protection[0]==4&&runs.size[1]==0x7c00000&&!runs.protection[1]);
        assert(!memcmp(memory+0x100901,req,12)&&!memcmp(memory+0x100801,param,16));assert(release(&cpu,reserved,0,0x8000));
        runs=(struct Runs){0};assert(tribes_virtual_memory_visit(base,inspect,&runs)&&!runs.count);
    }
    for(unsigned i=0;i<500;i++) {
        tribes_set_last_error(123);uint32_t p=classic(&cpu,0,196607,0x2000,4);assert(p&&!(p%65536)&&tribes_get_last_error()==123);
        struct Runs runs={0};assert(tribes_virtual_memory_visit(base,inspect,&runs)&&runs.count==1&&runs.size[0]==196608&&!runs.protection[0]);
        assert(classic(&cpu,p+32769,16383,0x1000,4)==p+32768);for(unsigned j=0;j<16384;j++)assert(!memory[p+32768+j]);memset(memory+p+32768,0x71,16384);
        assert(classic(&cpu,p+32769,16383,0x1000,2)==p+32768&&memory[p+32768]==0x71);memory[p+32769]=92; /* Recommit keeps RW/data. */
        assert(release(&cpu,p+32768,16384,0x4000));assert(classic(&cpu,p+32768,16384,0x1000,4)==p+32768);for(unsigned j=0;j<16384;j++)assert(!memory[p+32768+j]);assert(release(&cpu,p,0,0x8000));
        p=classic(&cpu,0x62008001,32767,0x2000,4);assert(p==0x62000000);runs=(struct Runs){0};assert(tribes_virtual_memory_visit(base,inspect,&runs)&&runs.count==1&&runs.size[0]==65536);assert(release(&cpu,p,0,0x8000));
    }
    assert(!classic(&cpu,0,0,0x3000,4)&&tribes_get_last_error()==87);assert(!classic(&cpu,0xfffff000,65536,0x2000,4)&&tribes_get_last_error()==87);
    assert(!classic(&cpu,0x62000000,65536,0x1000,4)&&tribes_get_last_error()==487);
    uint32_t req[3]={0x61000000,0x61ffffff,0x100000};uint64_t param[2]={1,0x100901};memcpy(memory+0x100901,req,12);memcpy(memory+0x100801,param,16);
    uint32_t p=allocate(&cpu,0,65536,0x3000,4,0x100801);assert(p>=req[0]&&p+65535<=req[1]&&!(p%req[2]));assert(!allocate(&cpu,p,65536,0x3000,4,0x100801)&&tribes_get_last_error()==87);assert(release(&cpu,p,0,0x8000));
    assert(!allocate(&cpu,0,0,0x3000,4,0)&&tribes_get_last_error()==87);assert(!allocate(&cpu,0,65536,0x3000,0x40,0)&&tribes_get_last_error()==87);
    assert(!allocate(&cpu,0x62000000,65536,0x1000,4,0)&&tribes_get_last_error()==487);
    for(unsigned i=0;i<500;i++) {
        uint32_t address=0,size=196607,args[7]={UINT32_MAX,0x100801,0x100811,0x2000,4,0,0};
        memset(memory+0x100800,0x79,24);memcpy(memory+args[1],&address,4);memcpy(memory+args[2],&size,4);tribes_set_last_error(123);
        assert(!invoke(&cpu,3,7,args)&&tribes_get_last_error()==123);memcpy(&address,memory+args[1],4);memcpy(&size,memory+args[2],4);assert(address&&!(address%65536)&&size==196608);
        assert(memory[0x100800]==0x79&&memory[0x100805]==0x79&&memory[0x100810]==0x79&&memory[0x100815]==0x79);
        uint32_t committed=address+16384+13,bytes=16371;memcpy(memory+args[1],&committed,4);memcpy(memory+args[2],&bytes,4);args[3]=0x1000;
        assert(!invoke(&cpu,3,7,args)&&tribes_get_last_error()==123);memcpy(&committed,memory+args[1],4);memcpy(&bytes,memory+args[2],4);assert(committed==address+16384&&bytes==16384);
        for(unsigned j=0;j<bytes;j++)assert(!memory[committed+j]);memset(memory+committed,0x53,bytes);
        args[4]=2;assert(!invoke(&cpu,3,7,args)&&memory[committed]==0x53);struct Runs view={0};assert(tribes_virtual_memory_visit(base,inspect,&view));assert(view.count==3&&view.protection[1]==2);
        args[4]=4;assert(!invoke(&cpu,3,7,args));memory[committed]=0x71;assert(release(&cpu,address,0,0x8000));
        address=0x62000003;size=65533;memcpy(memory+args[1],&address,4);memcpy(memory+args[2],&size,4);args[3]=0x3000;
        assert(!invoke(&cpu,3,7,args));memcpy(&address,memory+args[1],4);memcpy(&size,memory+args[2],4);assert(address==0x62000000&&size==65536);
        args[3]=0x2000;assert(invoke(&cpu,3,7,args)==0xc0000018&&tribes_get_last_error()==123);assert(!memcmp(memory+args[1],&address,4)&&!memcmp(memory+args[2],&size,4));assert(release(&cpu,address,0,0x8000));
        args[0]=0;assert(invoke(&cpu,3,7,args)==0xc0000008&&tribes_get_last_error()==123);args[0]=UINT32_MAX;
        size=0;memcpy(memory+args[2],&size,4);assert(invoke(&cpu,3,7,args)==0xc000000d&&tribes_get_last_error()==123);
        args[1]=0;assert(invoke(&cpu,3,7,args)==0xc0000005&&tribes_get_last_error()==123);
    }
    /* Nonzero final arguments must cross the actual seven-argument NT wrapper. */
    memcpy(memory+0x100901,req,12);memcpy(memory+0x100801,param,16);
    uint32_t nt_address=0,nt_size=65535,nt_args[7]={UINT32_MAX,0x100a01,0x100a11,0x3000,4,0x100801,1};
    memcpy(memory+nt_args[1],&nt_address,4);memcpy(memory+nt_args[2],&nt_size,4);tribes_set_last_error(123);
    assert(!invoke(&cpu,3,7,nt_args)&&tribes_get_last_error()==123);memcpy(&nt_address,memory+nt_args[1],4);memcpy(&nt_size,memory+nt_args[2],4);
    assert(nt_address>=req[0]&&nt_address+65535<=req[1]&&!(nt_address%req[2])&&nt_size==65536);assert(release(&cpu,nt_address,0,0x8000));
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,window));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    struct Runs runs={0};assert(tribes_virtual_memory_visit(base,inspect,&runs)&&!runs.count);assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 500 FromApp/free, 500 classic rounded-byte-range and 500 NT actual ARM64 seven-/four-/three-argument ABI lifecycle sequences, CPU/flags/stack guards, zero/recommit/decommit/release/reuse, mixed reservation inspection, aligned extended-address constraints, original 128 MiB reserve with 4 MiB alignment, 4 MiB classic commit and release, NT guest rounding/in-out guards/NTSTATUS/LastError and recommit protections, neighbor data retention, 4000 concurrent owned lifetimes and thread-local errors");return 0;
}
