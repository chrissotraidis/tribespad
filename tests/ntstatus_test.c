#include "../port/core/guest_ntstatus.h"
#include "../port/core/guest_teb.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern uint32_t reference_ntstatus(uint32_t);
extern const uint32_t reference_statuses[];
extern const unsigned reference_count;
extern void tbimp_RtlNtStatusToDosError(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){if(pc==1)return (void *)tbimp_RtlNtStatusToDosError;if(pc==0xfffff000)return (void *)tribes_return_to_host;abort();}
static unsigned char *memory;
static uint64_t base;
static void configure(unsigned slot){assert(tribes_teb_configure(base,0x30000+slot*4096,0x20000,0x10000,1,slot+1,0x40000));}
static void check(uint32_t status){
 uint32_t expected=reference_ntstatus(status);tribes_set_last_error(12345);
 assert(tribes_ntstatus_to_dos_error(status)==expected);
 assert(x86_read_fs_dword(0xbf4)==status&&tribes_get_last_error()==12345);
}
static void *worker(void *argument){
 unsigned slot=(unsigned)(uintptr_t)argument;configure(slot);uint32_t random=slot+1;
 for(unsigned i=0;i<25000;i++){random=random*1664525u+1013904223u;check(random);}
 tribes_teb_clear();return NULL;
}
static void fault(int kind){
 configure(0);uint32_t sentinel=0xabcdef01;memcpy(memory+0x30bf4,&sentinel,4);
 if(kind==0)assert(tribes_memory_permissions_set(0x30000,4096,1));
 if(kind==1)memset(memory+0x30018,0,4);
 pid_t pid=fork();assert(pid>=0);if(!pid){if(kind==2)tribes_teb_clear();(void)tribes_ntstatus_to_dos_error(0xc0000008);_exit(90);}
 int status;assert(waitpid(pid,&status,0)==pid&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 uint32_t direct;memcpy(&direct,memory+0x30bf4,4);assert(direct==sentinel);
 assert(tribes_memory_permissions_set(0x30000,4096,3));configure(0);
}
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x40000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));configure(0);
 assert(x86_read_fs_dword(0xbf4)==0);
 unsigned count=0;
 for(unsigned i=0;i<reference_count;i++){
  uint32_t v=reference_statuses[i];check(v);check(v-1);check(v+1);check(v|0x10000000);check(v|0x20000000);count+=5;
 }
 const uint32_t special[]={0,0xc0010005,0x80070006,0xc0070057,0xd0070057,0xc0010000,0x8007ffff,0xc007ffff,0xdeadbeef,0xffffffff};
 for(unsigned i=0;i<sizeof special/sizeof special[0];i++){check(special[i]);count++;}
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x1ffe1;uint32_t value=0xc0000008;memcpy(memory+cpu._esp,&value,4);_cpu saved=cpu;
 tribes_enter(&cpu,1);saved._eax=6;saved._esp+=4;assert(!memcmp(&cpu,&saved,sizeof cpu)&&x86_read_fs_dword(0xbf4)==value);
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(threads+i,NULL,worker,(void *)(uintptr_t)(i+1)));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(x86_read_fs_dword(0xbf4)==value);
 fault(0);fault(1);fault(2);
 printf("PASS: %u source/boundary cases, 200000 concurrent random comparisons, real guest ABI, isolated original NTSTATUS/LastError and protected/corrupt/unconfigured TEB rejection\n",count);
 return 0;
}
