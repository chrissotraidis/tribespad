#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
typedef struct {uint32_t r[12];double fp[8];uint64_t base;} CPU;
_Static_assert(sizeof(CPU)==120,"translated register prefix");
extern void baseline_cpu_batch(CPU *),optimized_cpu_batch(CPU *);
extern void baseline_cpu_block(CPU *),optimized_cpu_block(CPU *);
extern void optimized_cpu_outside(CPU *);
extern uint32_t baseline_route,optimized_route;
static unsigned char *allocation,*memory;
static void fault(unsigned n){
 CPU cpu={0};cpu.base=(uintptr_t)memory;cpu.r[9]=8192;
 switch(n){
 case 0:optimized_cpu_batch((CPU *)(memory+8192));break;
 case 1:optimized_cpu_batch((CPU *)(memory-116));break;
 case 2:optimized_cpu_batch((CPU *)(memory+65532));break;
 case 3:optimized_cpu_batch((CPU *)(UINTPTR_MAX-60));break;
 case 4:optimized_cpu_batch(NULL);break;
 case 5:tribes_memory_access_host_span(&cpu,119);break;
 case 6:assert(tribes_memory_permissions_set(8192,4096,1));optimized_cpu_batch(&cpu);break;
 case 7:assert(tribes_memory_permissions_set(8192,4096,0));optimized_cpu_batch(&cpu);break;
 case 8:assert(tribes_memory_permissions_set(0,4096,0));optimized_cpu_outside((CPU *)(memory-120));break;
 default:abort();
 }
}
static void *publisher(void *unused){(void)unused;for(unsigned i=0;i<4000;++i)assert(tribes_memory_permissions_set(4096,4096,(i&1)?1:3));return NULL;}
static void *worker(void *arg){
 CPU cpu={0};cpu.base=(uintptr_t)memory;cpu.r[9]=8192+(uintptr_t)arg*4096;
 for(unsigned i=0;i<2000;++i){baseline_cpu_batch(&cpu);optimized_cpu_batch(&cpu);}
 assert(*(uint32_t *)(memory+cpu.r[9])==32000);
 for(unsigned i=0;i<8;++i)assert(cpu.r[i]==4000);return NULL;
}
static double now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char **argv){
 (void)argv;allocation=mmap(0,98304,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(allocation!=MAP_FAILED);memory=allocation+16384;
 assert(tribes_memory_checks_configure((uintptr_t)memory,65536));
 unsigned char snapshot[98304];memcpy(snapshot,allocation,sizeof snapshot);
 if(argc==1)for(unsigned n=0;n<9;++n){
  pid_t child=fork();assert(child>=0);
  if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);alarm(5);freopen("/dev/null","w",stderr);fault(n);_exit(99);}
  int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(snapshot,allocation,sizeof snapshot));
 }
 // Compare one actual translated block, including flags and both continuations.
 for(unsigned n=0;n<256;++n){
  CPU a,b;unsigned char *bytes=(unsigned char *)&a;for(unsigned i=0;i<sizeof a;++i)bytes[i]=(unsigned char)(i*73+n*29);
  a.r[0]=4096;a.base=(uintptr_t)memory;b=a;uint32_t value=(n&1)?n*971u:0;memcpy(memory+4108,&value,4);memcpy(snapshot,allocation,sizeof snapshot);
  baseline_route=optimized_route=0;baseline_cpu_block(&a);optimized_cpu_block(&b);
  assert(!memcmp(&a,&b,sizeof a)&&baseline_route==optimized_route&&baseline_route!=0);assert(!memcmp(snapshot,allocation,sizeof snapshot));
 }
 pthread_t writer,threads[4];assert(!pthread_create(&writer,0,publisher,0));
 for(uintptr_t i=0;i<4;++i)assert(!pthread_create(threads+i,0,worker,(void *)i));
 for(unsigned i=0;i<4;++i)assert(!pthread_join(threads[i],0));assert(!pthread_join(writer,0));
 CPU cpu={0};cpu.base=(uintptr_t)memory;cpu.r[9]=32768;
 double start=now();for(unsigned i=0;i<200000;++i)baseline_cpu_batch(&cpu);double old=now()-start;
 start=now();for(unsigned i=0;i<200000;++i)optimized_cpu_batch(&cpu);double optimized=now()-start;
 assert(*(uint32_t *)(memory+32768)==3200000);for(unsigned i=0;i<8;++i)assert(cpu.r[i]==400000);
 printf("PASS: %u host-span/guest-access faults preserve all shared bytes; 256 original block CPU/flags/route comparisons; concurrent checked accesses/publication; baseline %.6f optimized %.6f seconds\n",argc==1?9u:0u,old,optimized);
}
