#include "../port/core/guest_processor_features.h"
#include "../port/core/guest_cpu.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t),tribes_return_to_host(void);
static unsigned char *memory;
static void *worker(void *arg){
 uint32_t stack=4096+(uint32_t)(uintptr_t)arg*4096;
 for(unsigned n=0;n<1000;n++)for(unsigned i=1;i<=3;i++){
  _cpu cpu,before;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=stack+257;cpu._eflags=n^0x202;before=cpu;
  memset(memory+stack,0x7b,4096);tribes_enter(&cpu,i);before._eax=i==1;
  assert(!memcmp(&cpu,&before,sizeof cpu));
  /* Only the return address pushed by the entry fixture changes stack bytes. */
  for(unsigned j=0;j<4096;j++)if(j<253||j>=257)assert(memory[stack+j]==0x7b);
 }
 return NULL;
}
int main(void){
 memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure((uintptr_t)memory,65536));
#endif
 _cpu cpu={0};cpu._eax=1;x86_cpuid(&cpu);
 assert(cpu._edx==0x00800001&&cpu._ecx==0);
 assert(tribes_sdl_has_mmx()==1&&tribes_processor_feature_present(3)==1);
 assert(!tribes_sdl_has_sse()&&!tribes_processor_feature_present(6)&&!tribes_sdl_has_avx());
 pthread_t threads[8];for(uintptr_t i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
 puts("PASS: 24000 concurrent emitted SDL CPU queries agree with guest CPUID: MMX=true SSE=false AVX=false; CPU and cdecl stack preserved");
}
