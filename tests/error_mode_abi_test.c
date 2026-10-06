#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern uint32_t tribes_set_error_mode(uint32_t),tribes_get_error_mode(void);
extern void tbimp_SetErrorMode(void),tbimp_GetErrorMode(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_SetErrorMode;if(address==2)return (void *)tbimp_GetErrorMode;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,uint32_t mode){
 unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;uint32_t top=cpu->_esp;
 if(target==1){cpu->_esp-=4;memcpy(memory+cpu->_esp,&mode,4);}
 _cpu expected=*cpu;tribes_enter(cpu,target);expected._eax=cpu->_eax;expected._esp=top;assert(!memcmp(cpu,&expected,sizeof expected));
 if(target==1){uint32_t unchanged;memcpy(&unchanged,memory+top-4,4);assert(unchanged==mode);}return cpu->_eax;
}
static void *worker(void *arg){
 uint32_t requested=(uint32_t)(uintptr_t)arg;
 for(unsigned i=0;i<1000;i++){uint32_t previous=tribes_set_error_mode(requested);assert((previous&4)&&!(previous&~0x8007u));assert(tribes_get_error_mode()&4);}return NULL;
}
int main(void){
 unsigned char memory[4096];memset(memory,0xa5,sizeof memory);_cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=2049;
 uint32_t expected=0;assert(!invoke(&cpu,2,0));
 for(unsigned sticky=0;sticky<2;sticky++)for(unsigned i=0;i<8;i++)for(unsigned j=0;j<8;j++){
  uint32_t modes[2]={((i&4)?0x8000:0)|(i&3)|(sticky?4:0),((j&4)?0x8000:0)|(j&3)};
  for(unsigned k=0;k<2;k++){tribes_set_last_error(12345);assert(invoke(&cpu,1,modes[k])==expected);expected=modes[k]|(expected&4);assert(invoke(&cpu,2,0)==expected&&tribes_get_last_error()==12345);}
 }
 pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(threads+i,NULL,worker,(void *)(uintptr_t)(i|(i&1?0x8000:0))));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(tribes_get_error_mode()&4);
 for(unsigned i=0;i<2037;i++)assert(memory[i]==0xa5);for(unsigned i=2049;i<4096;i++)assert(memory[i]==0xa5);
 puts("PASS: emitted Set/GetErrorMode ABI, 256 old/new transitions, sticky alignment bit, last-error/CPU/stack preservation and 4000 concurrent process-wide updates");return 0;
}
