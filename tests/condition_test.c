#include "../port/core/guest_condition.h"
#include "../port/core/guest_srw.h"
#include "../port/core/critical_section.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;static const uint32_t cv=0x1001,lock=0x2001,cs=0x3001;
static atomic_uint returned;static unsigned generation;
static uint64_t now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static uint32_t word(uint32_t p){uint32_t n;memcpy(&n,memory+p,4);return n;}
static uint32_t call(unsigned slot,unsigned id,const uint32_t *args,unsigned n){
 _cpu c;memset(&c,0xa5,sizeof c);c._pointer_offset=base;c._esp=0x10003+slot*4096;uint32_t sp=c._esp;
 memset(memory+sp-68,0x71,64);memset(memory+sp+4*n,0x71,64);memcpy(memory+sp,args,4*n);_cpu before=c;
 tribes_set_last_error(12345);tribes_enter(&c,id);if(id>=4)before._eax=c._eax;before._esp+=4*n;
 assert(!memcmp(&before,&c,sizeof c));assert(!memcmp(memory+sp,args,4*n));
 for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x71&&memory[sp+4*n+i]==0x71);
 assert(tribes_get_last_error()==((id>=4&&!c._eax)?1460:12345));return c._eax;
}
static void take(unsigned mode){if(mode==2)tribes_critical_enter(base,cs);else if(mode==1)tribes_srw_acquire_shared(base,lock);else tribes_srw_acquire_exclusive(base,lock);}
static void drop(unsigned mode){if(mode==2)tribes_critical_leave(base,cs);else if(mode==1)tribes_srw_release_shared(base,lock);else tribes_srw_release_exclusive(base,lock);}
static uint32_t sleep_for(unsigned slot,unsigned mode,uint32_t ms){uint32_t args[]={cv,mode==2?cs:lock,ms,mode};return call(slot,mode==2?5:4,args,mode==2?3:4);}
static void wait_count(unsigned n){uint64_t end=now()+UINT64_C(10000000000);while(tribes_condition_waiters(base,cv)!=n){assert(now()<end);sched_yield();}}
struct Worker{unsigned slot,mode;uint32_t ms;};
static void *worker(void *arg){struct Worker *w=arg;take(w->mode);while(!generation)assert(sleep_for(w->slot,w->mode,w->ms));assert(generation==71);atomic_fetch_add(&returned,1);drop(w->mode);return NULL;}
static void *foreign(void *arg){(void)arg;tribes_condition_sleep_srw(base,cv,lock,0,0);return NULL;}
static void fault(const char *name){
 if(!strcmp(name,"null"))tribes_condition_initialize(base,0);
 else if(!strcmp(name,"overflow"))tribes_condition_initialize(base,UINT32_MAX-2);
 else if(!strcmp(name,"nonzero")){memset(memory+cv,1,4);tribes_condition_wake(base,cv);}
 else if(!strcmp(name,"unowned"))tribes_condition_sleep_srw(base,cv,lock,0,0);
 else if(!strcmp(name,"mode")){take(0);tribes_condition_sleep_srw(base,cv,lock,0,1);}
 else if(!strcmp(name,"flags"))tribes_condition_sleep_srw(base,cv,lock,0,2);
 else if(!strcmp(name,"recursive-cs")){take(2);take(2);tribes_condition_sleep_cs(base,cv,cs,0);}
 else if(!strcmp(name,"unowned-cs"))tribes_condition_sleep_cs(base,cv,cs,0);
 else if(!strcmp(name,"foreign")){take(0);pthread_t t;assert(!pthread_create(&t,NULL,foreign,NULL));pthread_join(t,NULL);}
 else {
  struct Worker w={1,2,UINT32_MAX};pthread_t t;assert(!pthread_create(&t,NULL,worker,&w));wait_count(1);
  if(!strcmp(name,"busy-init"))tribes_condition_initialize(base,cv);
  else if(!strcmp(name,"copied-live")){memcpy(memory+cv+16,memory+cv,4);tribes_condition_wake(base,cv+16);}
  else if(!strcmp(name,"delete-cs"))tribes_critical_delete(base,cs);
  else abort();
 }
 abort();
}
#ifdef CHECKED_TEST
extern void condition_guest_pointer(void *,void *,void *),condition_wrong_abi(void),condition_signal_guest(void *),condition_signal_wrong(void);
static void reject(unsigned mode){
 memset(memory+0x7fff0,0x5a,32);pid_t child=fork();assert(child>=0);
 if(!child){struct rlimit lim={0,0};setrlimit(RLIMIT_CORE,&lim);
  if(mode<2){assert(tribes_memory_permissions_set(0x80000,4096,1));tribes_condition_initialize(base,mode?0x7fffe:0x80000);}
  else if(mode==2){assert(tribes_memory_permissions_set(0x80000,4096,0));tribes_condition_wake(base,0x80000);}
  else if(mode<6){pthread_cond_t c=PTHREAD_COND_INITIALIZER;pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;struct timespec t={0,0};condition_guest_pointer(mode==3?(void *)(memory+0x1000):&c,mode==4?(void *)(memory+0x1000):&m,mode==5?(void *)(memory+0x1000):&t);}
  else if(mode==6)condition_wrong_abi();
  else if(mode==7)condition_signal_guest(memory+0x1000);
  else condition_signal_wrong();
  _exit(99);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 for(unsigned i=0;i<32;i++)assert(memory[0x7fff0+i]==0x5a);
}
#endif
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));base=(uintptr_t)memory;
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 tribes_critical_init(base,cs);if(argc>1){fault(argv[1]);return 99;}
 for(unsigned mode=0;mode<3;mode++){
  memset(memory+cv-1,0x71,6);call(0,1,&cv,1);assert(!word(cv));call(0,2,&cv,1);call(0,3,&cv,1);
  take(mode);assert(!sleep_for(0,mode,0));uint64_t start=now();assert(!sleep_for(0,mode,15));assert(now()-start>=UINT64_C(15000000));drop(mode);
  assert(!word(cv)&&!tribes_condition_live_count());assert(memory[cv-1]==0x71&&memory[cv+4]==0x71);
  memcpy(memory+cv+16,memory+cv,4);tribes_condition_wake_all(base,cv+16);
  for(unsigned iteration=0;iteration<24;iteration++){
   generation=0;atomic_store(&returned,0);pthread_t threads[6];struct Worker workers[6];
   for(unsigned i=0;i<6;i++){workers[i]=(struct Worker){i+1,mode,iteration%2?UINT32_MAX:10000};assert(!pthread_create(&threads[i],NULL,worker,&workers[i]));}
   wait_count(6);
   /* Hold an exclusive lock across waking to prove wake cannot return without
      reacquiring. Also verifies visibility of the predicate written here. */
   take(mode==1?0:mode);generation=71;call(0,2,&cv,1);wait_count(5);assert(!atomic_load(&returned));
   call(0,3,&cv,1);wait_count(0);assert(!atomic_load(&returned));drop(mode==1?0:mode);
   for(unsigned i=0;i<6;i++)assert(!pthread_join(threads[i],NULL));assert(atomic_load(&returned)==6);
   assert(!word(cv)&&!tribes_condition_live_count()&&!tribes_srw_live_count());
  }
 }
 tribes_critical_delete(base,cs);
#ifdef CHECKED_TEST
 for(unsigned i=0;i<9;i++)reject(i);
#endif
 puts("PASS: five emitted condition-variable ABI wrappers; all CPU/flags/stack and success/timeout LastError; exact unaligned 4-byte object; 432 finite/infinite concurrent waits over exclusive/shared SRW and CS, wake-one/all, atomic release/reacquire and predicate visibility, zero/15ms timeout with no remembered wake, idle metadata retirement and movable zero state");return 0;
}
