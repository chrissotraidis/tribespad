#include "../port/core/guest_scheduling.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;static uint32_t mask;
static uint32_t read32(uint32_t p){uint32_t n;memcpy(&n,memory+p,4);return n;}
static uint32_t call(unsigned id,const uint32_t *args,unsigned n){
 _cpu c;memset(&c,0xa5,sizeof c);c._pointer_offset=base;c._esp=0xc003;memcpy(memory+c._esp,args,4*n);_cpu before=c;
 tribes_enter(&c,id);before._eax=c._eax;before._esp+=4*n;assert(!memcmp(&before,&c,sizeof c));assert(!memcmp(memory+0xc003,args,4*n));return c._eax;
}
static void *worker(void *arg){
 uint32_t p=0x10000+(uint32_t)(uintptr_t)arg*4096;
 for(unsigned n=0;n<32;n++){
  tribes_set_last_error(p);assert(tribes_get_process_affinity_mask(base,UINT32_MAX,p,p+4));assert(read32(p)==mask&&read32(p+4)==mask&&tribes_get_last_error()==p);
  assert(!tribes_set_thread_affinity_mask(UINT32_MAX-1,mask)&&tribes_get_last_error()==50);
 }
 return NULL;
}
#if defined(CHECKED_TEST) && !defined(SIMULATOR_TEST)
extern void scheduling_wrong_selector(void),scheduling_wrong_process(void),scheduling_wrong_abi(void),scheduling_unreviewed(void);
static void reject(unsigned mode){
 pid_t child=fork();assert(child>=0);if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);
  if(mode<2){assert(tribes_memory_permissions_set(0x20000,4096,1));tribes_get_process_affinity_mask(base,UINT32_MAX,mode?0x1000:0x20000,mode?0x20000:0x1000);}
  if(mode==2)scheduling_wrong_selector();if(mode==3)scheduling_wrong_process();if(mode==4)scheduling_wrong_abi();if(mode==5)scheduling_unreviewed();_exit(99);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
#endif
int main(int argc,char **argv){
 assert(argc==2);unsigned priority=(unsigned)strtoul(argv[1],NULL,0);
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));base=(uintptr_t)memory;
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 long count=sysconf(_SC_NPROCESSORS_ONLN);assert(count>0);mask=count>=32?UINT32_MAX:(1u<<count)-1;
 tribes_set_last_error(123);uint32_t query[]={UINT32_MAX,0x1001,0x1007};assert(call(2,query,3)&&read32(0x1001)==mask&&read32(0x1007)==mask&&tribes_get_last_error()==123);
 query[2]=query[1];assert(call(2,query,3)&&read32(0x1001)==mask);query[0]=17;assert(!call(2,query,3)&&tribes_get_last_error()==6);query[0]=UINT32_MAX;
 query[1]=0;assert(!call(2,query,3)&&tribes_get_last_error()==87);query[1]=UINT32_MAX-1;assert(!call(2,query,3)&&tribes_get_last_error()==87);
 uint32_t affinity[]={UINT32_MAX-1,0};assert(!call(3,affinity,2)&&tribes_get_last_error()==87);affinity[1]=mask;assert(!call(3,affinity,2)&&tribes_get_last_error()==50);
 affinity[1]=1;assert(!call(3,affinity,2)&&tribes_get_last_error()==50);if(mask!=UINT32_MAX){affinity[1]=mask+1;assert(!call(3,affinity,2)&&tribes_get_last_error()==87);}
 affinity[0]=123;assert(!call(3,affinity,2)&&tribes_get_last_error()==6);
 uint32_t bad[]={123,0x8000};assert(!call(1,bad,2)&&tribes_get_last_error()==6);bad[0]=UINT32_MAX;bad[1]=0;assert(!call(1,bad,2)&&tribes_get_last_error()==87);
 for(unsigned p=0;p<3;p++){bad[1]=(uint32_t[]){0x100,0x100000,0x200000}[p];assert(!call(1,bad,2)&&tribes_get_last_error()==50);}
#if defined(CHECKED_TEST) && !defined(SIMULATOR_TEST)
 for(unsigned i=0;i<6;i++)reject(i);
#endif
 pthread_t workers[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&workers[i],NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(workers[i],NULL));
 int nice=priority==0x40?19:priority==0x4000?5:priority==0x20?0:priority==0x8000?-5:-10;
 errno=0;int before=getpriority(PRIO_PROCESS,0);assert(!errno);uint32_t args[]={UINT32_MAX,priority};tribes_set_last_error(123);uint32_t result=call(1,args,2),error=tribes_get_last_error();errno=0;int after=getpriority(PRIO_PROCESS,0);assert(!errno);
 if(result)assert(after==nice&&error==123);else assert(after==before);
 /* Independent native retry checks the actual host outcome, without asking
    tests to assume a privilege level or a particular starting niceness. */
 errno=0;int native=setpriority(PRIO_PROCESS,0,nice),host_error=errno;assert(result==(native==0));
 if(native)assert((host_error==EACCES||host_error==EPERM)&&error==5);
 printf("PASS: class=0x%x native nice %d -> %d result=%u error=%u; unrestricted mask=0x%x; 128 concurrent affinity snapshots; strict pinning rejected; emitted ABI and guards\n",priority,before,after,result,error,mask);
}
