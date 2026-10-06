#include "../port/core/multimedia_timer.h"
#include "../port/core/performance_clock.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;static uint32_t minimum;
static uint32_t read32(uint32_t p){uint32_t n;memcpy(&n,memory+p,4);return n;}
static uint64_t now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static uint32_t call(unsigned id,const uint32_t *args,unsigned n){
 _cpu c;memset(&c,0xa5,sizeof c);c._pointer_offset=base;c._esp=0xc003;
 if(n)memcpy(memory+c._esp,args,4*n);_cpu before=c;
 tribes_set_last_error(12345);tribes_enter(&c,id);before._eax=c._eax;before._esp+=4*n;
 assert(!memcmp(&before,&c,sizeof c));if(n)assert(!memcmp(memory+0xc003,args,4*n));assert(tribes_get_last_error()==12345);return c._eax;
}
static void *worker(void *arg){
 uint32_t p=0x10000+(uint32_t)(uintptr_t)arg*4096;
 for(unsigned i=0;i<128;i++){
  tribes_set_last_error(p);assert(!tribes_time_caps(base,p,8));assert(read32(p)==minimum&&read32(p+4)==65535);
  assert(!tribes_time_begin_period(minimum));assert(!tribes_time_begin_period(minimum));
  assert(!tribes_time_end_period(minimum));assert(!tribes_time_end_period(minimum));assert(tribes_get_last_error()==p);
 }
 return NULL;
}
#ifdef CHECKED_TEST
extern void timer_guest_pointer(void *),timer_wrong_abi(void),timer_wrong_clock(void);
static void reject(unsigned mode){
 memset(memory+0x1fff0,0x5a,32);
 pid_t child=fork();assert(child>=0);if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);
  if(mode<2){assert(tribes_memory_permissions_set(0x20000,4096,1));tribes_time_caps(base,mode?0x1fffc:0x20000,8);}
  if(mode==2)timer_guest_pointer(memory+0x1000);
  if(mode==3)timer_wrong_abi();if(mode==4)timer_wrong_clock();_exit(99);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 for(unsigned i=0;i<32;i++)assert(memory[0x1fff0+i]==0x5a);
}
#endif
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));base=(uintptr_t)memory;
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 struct timespec resolution;assert(!clock_getres(CLOCK_MONOTONIC,&resolution));
 minimum=(uint32_t)(((uint64_t)resolution.tv_sec*1000000000+resolution.tv_nsec+999999)/1000000);assert(minimum&&minimum<=65535);
 for(unsigned size=0;size<33;size++){
  memset(memory+0x1000,0x6d,64);uint32_t args[]={0x1001,size};uint32_t result=call(1,args,2);assert(result==(size<8?97:0));
  if(size>=8)assert(read32(0x1001)==minimum&&read32(0x1005)==65535);
  for(unsigned j=0;j<64;j++)if(size<8||j<1||j>=9)assert(memory[0x1000+j]==0x6d);
 }
 uint32_t args[]={0x1001,UINT32_MAX};assert(!call(1,args,2));args[0]=0;assert(call(1,args,2)==97);args[0]=UINT32_MAX-6;assert(call(1,args,2)==97);
 assert(tribes_time_caps(0,0x1000,8)==97&&tribes_time_caps(UINT64_MAX-3,4,8)==97);
 for(uint32_t p=minimum;p<=65535;p++){assert(!call(2,&p,1));assert(!call(3,&p,1));}
 for(unsigned i=0;i<3;i++){uint32_t p=(uint32_t[]){0,65536,UINT32_MAX}[i];assert(call(2,&p,1)==97&&call(3,&p,1)==97);}
 assert(call(3,&minimum,1)==97);assert(!call(2,&minimum,1));assert(!call(2,&minimum,1));assert(!call(3,&minimum,1));assert(!call(3,&minimum,1));assert(call(3,&minimum,1)==97);
 pthread_t threads[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(tribes_time_end_period(minimum)==97);
 for(unsigned i=0;i<16;i++){
  uint64_t start=now();uint32_t tick=call(4,NULL,0);uint64_t end=now();assert((uint32_t)(tick-(uint32_t)(start/1000000))<=(uint32_t)(end/1000000-start/1000000));
 }
 assert(!tribes_time_begin_period(minimum));uint64_t before=now();tribes_sleep(minimum);uint64_t elapsed=now()-before;assert(elapsed>=minimum*UINT64_C(1000000));assert(!tribes_time_end_period(minimum));
#ifdef CHECKED_TEST
 for(unsigned i=0;i<5;i++)reject(i);
#endif
 printf("PASS: native resolution=%lldns minimum=%ums; full1..65535 period range, matched references, 512 concurrent queries, 33 buffer sizes, emitted ABI/LastError/clock bounds; delay=%lluns\n",(long long)resolution.tv_sec*1000000000+resolution.tv_nsec,minimum,(unsigned long long)elapsed);
}
