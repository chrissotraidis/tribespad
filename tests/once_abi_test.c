#include "../port/core/guest_once_callback.h"
#include "../port/core/guest_once.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
extern void tbimp_InitOnceExecuteOnce(void),tbimp_InitOnceInitialize(void),fixture_callback(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){if(pc==1)return (void *)tbimp_InitOnceExecuteOnce;if(pc==2)return (void *)fixture_callback;if(pc==3)return (void *)tbimp_InitOnceInitialize;if(pc==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static unsigned char *memory;
static atomic_uint calls,active,failures,ready;
static int fault_mode;
static _Thread_local unsigned thread_callbacks;
uint32_t fixture_body(_cpu *cpu,uint32_t once,uint32_t parameter,uint32_t context){
    thread_callbacks++;assert(once==0x200001&&parameter==0x1234);assert(!atomic_fetch_add(&active,1));unsigned call=atomic_fetch_add(&calls,1);usleep(1000);
    cpu->_ecx=0xdeadbeef;cpu->_eflags=0x246;cpu->_st_cw=0xb7f;
    if(fault_mode==1){uint32_t value; (void)tribes_once_begin(cpu->_pointer_offset,once,&value);abort();}
    if(fault_mode==2){uint32_t invalid=3;memcpy(memory+context,&invalid,4);atomic_fetch_sub(&active,1);return 1;}
    if(call<3){atomic_fetch_sub(&active,1);tribes_set_last_error(77);return 0;}
    uint32_t value=0x200400,payload=0x13579bdf;memcpy(memory+context,&value,4);memcpy(memory+value,&payload,4);atomic_fetch_sub(&active,1);return 1;
}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args){
    uint32_t top=cpu->_esp,sp=top-4*count;memcpy(memory+sp,args,4*count);memset(memory+sp-68,0x79,64);memset(memory+top,0x79,64);cpu->_esp=sp;_cpu before=*cpu;unsigned oldcalls=thread_callbacks;
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;
    /* Callback changes thread FP state, parent general registers/flags stay intact. */
    if(target==1&&thread_callbacks>oldcalls)before._st_cw=0xb7f;
    assert(!memcmp(cpu,&before,sizeof before));for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x79&&memory[top+i]==0x79);return result;
}
static void setup(_cpu *cpu,unsigned i){memset(cpu,0xa5,sizeof *cpu);cpu->_pointer_offset=(uintptr_t)memory;cpu->_esp=0x100000+i*65536+0xf003;cpu->stack_bottom=memory+0x100000+i*65536;cpu->stack_top=memory+0x100000+(i+1)*65536;}
static void *worker(void *context){_cpu cpu;setup(&cpu,(unsigned)(uintptr_t)context);atomic_fetch_add(&ready,1);while(atomic_load(&ready)<8){}uint32_t output=cpu._esp-512;
    for(unsigned i=0;i<100;i++){
        uint32_t args[]={0x200001,2,0x1234,output};memset(memory+output-1,0x79,6);tribes_set_last_error(123);uint32_t result=invoke(&cpu,1,4,args);
        if(!result){assert(tribes_get_last_error()==77);for(unsigned k=0;k<4;k++)assert(memory[output+k]==0x79);atomic_fetch_add(&failures,1);i--;continue;}
        uint32_t value,payload;memcpy(&value,memory+output,4);assert(value==0x200400);memcpy(&payload,memory+value,4);assert(payload==0x13579bdf);assert(memory[output-1]==0x79&&memory[output+4]==0x79&&tribes_get_last_error()==123);
    }return NULL;
}
int main(int argc,char **argv){memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory+0x100000,0x110000,PROT_READ|PROT_WRITE));_cpu cpu;setup(&cpu,0);
    if(argc>1){uint32_t args[]={0x200001,2,0x1234,0};
        if(!strcmp(argv[1],"recursive"))fault_mode=1;
        else if(!strcmp(argv[1],"reserved-context")){fault_mode=2;atomic_store(&calls,3);}
        else if(!strcmp(argv[1],"unowned-word")){uint32_t v=8;memcpy(memory+0x200001,&v,4);}
        else if(!strcmp(argv[1],"foreign-complete")){(void)tribes_once_complete((uintptr_t)memory,0x200001,1,0);return 99;}
        (void)invoke(&cpu,1,4,args);return 99;
    }
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(atomic_load(&calls)==4&&atomic_load(&failures)==3);
    uint32_t once=0x200001;tribes_set_last_error(123);(void)invoke(&cpu,3,1,&once);uint32_t word;memcpy(&word,memory+once,4);assert(!word&&tribes_get_last_error()==123);
    uint32_t args[]={once,2,0x1234,0};assert(invoke(&cpu,1,4,args));assert(atomic_load(&calls)==5&&tribes_get_last_error()==123);
    assert(memory[once-1]==0&&memory[once+4]==0);assert(!munmap(memory,UINT64_C(0x100000000)));
    puts("PASS: 800 actual ARM64 once-wrapper calls on eight threads, three callback failures/retries then one successful publication, single active three-argument stdcall callback, shared context/payload visibility, independent LastError, guarded unaligned DWORD object/output, caller CPU/flags/stack and shared FP state; reset/reinitialization and NULL output");return 0;
}
