#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "variants.inc"
static void *worker(void *argument){
 uint32_t seed=25034+(uint32_t)(uintptr_t)argument;
 for(unsigned i=0;i<8192;i++){
  _cpu cpu;memset(&cpu,0x9d,sizeof cpu);uint32_t *gpr=(uint32_t *)&cpu;
  for(unsigned j=0;j<9;j++){seed=seed*1664525+1013904223;gpr[j]=seed;}
  unsigned index=i%(sizeof bodies/sizeof *bodies),source=variants[index].source,destination=variants[index].destination;
  uint32_t value=i%16?gpr[source]:0;gpr[source]=value;_cpu expected=cpu;uint32_t result=0;
  while(result<32&&!(value&(UINT32_C(1)<<result)))result++;
  ((uint32_t *)&expected)[destination]=result;expected._eflags=(expected._eflags&~0x41u)|(value?0:1)|(result?0:0x40);
  bodies[index](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu));
 }
 return NULL;
}
int main(void){pthread_t threads[8];for(uintptr_t i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));puts("PASS: 65536 concurrent TZCNT translations, eight private CPUs, zero inputs, all register aliases and complete CPU/defined-flags preservation under ThreadSanitizer");}
