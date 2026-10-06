#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fences.inc"
static atomic_uint phase,done[2],data[2];static unsigned observed[2];
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static void *worker(void *opaque){unsigned id=(unsigned)(uintptr_t)opaque;_cpu cpu={0};for(unsigned n=1;n<=20000;++n){while(atomic_load_explicit(&phase,memory_order_acquire)!=n)sched_yield();atomic_store_explicit(&data[id],1,memory_order_relaxed);bodies[9](&cpu);observed[id]=atomic_load_explicit(&data[1-id],memory_order_relaxed);atomic_store_explicit(&done[id],n,memory_order_release);}return NULL;}
int main(void){unsigned cases=0;uint32_t seed=0xf3ce;
for(unsigned r=0;r<sizeof bodies/sizeof bodies[0];++r)for(unsigned j=0;j<1024;++j){_cpu cpu;for(unsigned k=0;k<sizeof cpu;++k)((unsigned char *)&cpu)[k]=(unsigned char)rnd(&seed);_cpu expected=cpu;bodies[r](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu));++cases;}
pthread_t threads[2];assert(!pthread_create(&threads[0],NULL,worker,(void *)(uintptr_t)0));assert(!pthread_create(&threads[1],NULL,worker,(void *)(uintptr_t)1));
for(unsigned n=1;n<=20000;++n){atomic_store_explicit(&data[0],0,memory_order_relaxed);atomic_store_explicit(&data[1],0,memory_order_relaxed);atomic_store_explicit(&phase,n,memory_order_release);while(atomic_load_explicit(&done[0],memory_order_acquire)!=n||atomic_load_explicit(&done[1],memory_order_acquire)!=n)sched_yield();assert(observed[0]||observed[1]);}
assert(!pthread_join(threads[0],NULL));assert(!pthread_join(threads[1],NULL));printf("PASS: %u full CPU fence checks; 20000 MFENCE store-buffer rounds without forbidden outcome\n",cases);return 0;}
