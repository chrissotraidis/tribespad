#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include "../port/core/guest_system.h"
extern void tbimp_GetSystemInfo(void),tbimp_GetCurrentProcessorNumberEx(void),tbimp_GetNumaProcessorNodeEx(void),tbimp_GetNumaProcessorNode(void),tbimp_GetNumaNodeProcessorMaskEx(void),tbimp_GetNumaHighestNodeNumber(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);
extern void x86_cpuid(_cpu *);
void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_GetSystemInfo;if(address==2)return (void *)tbimp_GetCurrentProcessorNumberEx;if(address==3)return (void *)tbimp_GetNumaProcessorNodeEx;if(address==4)return (void *)tbimp_GetNumaProcessorNode;if(address==5)return (void *)tbimp_GetNumaNodeProcessorMaskEx;if(address==6)return (void *)tbimp_GetNumaHighestNodeNumber;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
typedef struct Worker { uint64_t base;uint32_t output,count; } Worker;
static void *worker(void *context) {
    Worker *w=context;unsigned char *memory=(void *)(uintptr_t)w->base;
    for(unsigned i=0;i<1000;i++) {
        tribes_set_last_error(123);tribes_get_current_processor_number_ex(w->base,w->output);assert(!memory[w->output]&&!memory[w->output+1]&&memory[w->output+2]<w->count&&!memory[w->output+3]);
        assert(tribes_get_numa_processor_node_ex(w->base,w->output,w->output+5)&&!memory[w->output+5]&&!memory[w->output+6]);
        assert(tribes_get_numa_node_processor_mask_ex(w->base,0,w->output+9));uint32_t mask;memcpy(&mask,memory+w->output+9,4);assert(__builtin_popcount(mask)==(int)w->count&&tribes_get_last_error()==123);
    }return NULL;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);
    assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));unsigned char *memory=window;
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    _cpu identity;memset(&identity,0,sizeof identity);identity._eax=1;x86_cpuid(&identity);uint32_t signature=identity._eax;
    long online=sysconf(_SC_NPROCESSORS_ONLN);assert(online>0);uint32_t count=online>32?32:(uint32_t)online;
    for(unsigned i=0;i<1000;i++) {
        uint32_t output=0x100801+i%3;memset(memory+output-1,0x79,40);tribes_set_last_error(123);cpu._eax=i*73+17;uint32_t saved=cpu._eax;
        assert(invoke(&cpu,1,1,&output)==saved&&tribes_get_last_error()==123);
        uint32_t words[9];memcpy(words,memory+output,36);
        assert(!words[0]&&words[1]==4096&&words[2]==0x10000&&words[3]==0x7ffeffff&&words[7]==0x10000);
        assert(words[6]==586&&(words[8]&0xffff)==((signature>>8)&15)&&words[8]>>16==(((signature>>4)&15)<<8|(signature&15)));
        assert(words[5]==count&&__builtin_popcount(words[4])==(int)count&&words[4]==(count==32?UINT32_MAX:(1u<<count)-1));
        assert(memory[output-1]==0x79);for(unsigned n=36;n<39;n++)assert(memory[output+n]==0x79);
    }
    uint32_t current=0x100901,node=0x100911,mask=0x100921,highest=0x100941;
    for(unsigned i=0;i<1000;i++) {
        memset(memory+current-1,0x79,6);memset(memory+node-1,0x79,4);memset(memory+mask-1,0x79,14);tribes_set_last_error(123);uint32_t saved=cpu._eax;
        assert(invoke(&cpu,2,1,&current)==saved);assert(!memory[current]&&!memory[current+1]&&memory[current+2]<count&&!memory[current+3]);assert(memory[current-1]==0x79&&memory[current+4]==0x79);
        uint32_t args[2]={current,node};assert(invoke(&cpu,3,2,args));assert(!memory[node]&&!memory[node+1]&&memory[node-1]==0x79&&memory[node+2]==0x79);
        memset(memory+highest-1,0x79,6);assert(invoke(&cpu,6,1,&highest)&&tribes_get_last_error()==123);uint32_t highest_node;memcpy(&highest_node,memory+highest,4);assert(!highest_node&&memory[highest-1]==0x79&&memory[highest+4]==0x79);
        args[0]=highest_node|0x10000;args[1]=mask;assert(invoke(&cpu,5,2,args));uint32_t words[3];memcpy(words,memory+mask,12);assert(words[0]==(count==32?UINT32_MAX:(1u<<count)-1)&&!words[1]&&!words[2]);assert(memory[mask-1]==0x79&&memory[mask+12]==0x79);
        args[0]=0x100|memory[current+2];args[1]=node;memory[node+1]=0x79;assert(invoke(&cpu,4,2,args)&&!memory[node]&&memory[node+1]==0x79&&tribes_get_last_error()==123);
    }
    for(unsigned number=0;number<256;number++) {
        uint32_t args[2]={number,node};tribes_set_last_error(123);int valid=number<count;assert(invoke(&cpu,4,2,args)==(uint32_t)valid);assert(memory[node]==(valid?0:255)&&tribes_get_last_error()==(valid?123:87));
        memory[current]=memory[current+1]=memory[current+3]=0;memory[current+2]=(unsigned char)number;args[0]=current;tribes_set_last_error(123);assert(invoke(&cpu,3,2,args)==(uint32_t)valid);uint16_t value;memcpy(&value,memory+node,2);assert(value==(valid?0:65535)&&tribes_get_last_error()==(valid?123:87));
    }
    uint32_t args[2]={current,node};memory[current]=1;memory[current+2]=0;assert(!invoke(&cpu,3,2,args)&&tribes_get_last_error()==87);assert(memory[node]==255&&memory[node+1]==255);
    memset(memory+mask,0x79,12);args[0]=1;args[1]=mask;assert(!invoke(&cpu,5,2,args)&&tribes_get_last_error()==87);for(unsigned i=0;i<12;i++)assert(memory[mask+i]==0x79);
    uint32_t invalid=0;assert(!invoke(&cpu,6,1,&invalid)&&tribes_get_last_error()==87);invalid=0xfffffffd;assert(!invoke(&cpu,6,1,&invalid)&&tribes_get_last_error()==87);
    Worker workers[8];pthread_t threads[8];for(unsigned i=0;i<8;i++){workers[i]=(Worker){(uintptr_t)window,0x101000+i*4096,count};assert(!pthread_create(&threads[i],NULL,worker,&workers[i]));}for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 1000 SYSTEM_INFO and 5000 actual ARM64 processor/node/highest-node/mask stdcall calls, all 256 processor indices, BYTE/USHORT input truncation, 24000 concurrent native sample/topology calls, one UMA node/group coherence, exact 36-byte x86 pointer/mask layout, unaligned guards, CPUID family/model/stepping coherence, native processor count/mask, logical 4 KiB/64 KiB guest geometry, EAX/CPU/flags/stack/LastError preservation; no native subpage enforcement claim");return 0;
}
