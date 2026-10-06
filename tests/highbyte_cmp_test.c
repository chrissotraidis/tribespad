/* Exhaustive byte subtraction and full virtual CPU/memory preservation. */
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "cases.inc"
static uint32_t seed=0x382f2503;
static uint32_t random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint32_t subtract_flags(unsigned a,unsigned b){
 unsigned r=(a-b)&255,ones=0;for(unsigned i=0;i<8;i++)ones+=(r>>i)&1;
 return (a<b?1:0)|(!(ones&1)?4:0)|((a^b^r)&16)|(r==0?64:0)|(r&128)|(((a^b)&(a^r)&128)<<4);
}
static uint32_t address(_cpu *c,const Case *k){uint32_t g[8];memcpy(g,c,32);return g[k->base]+(k->index<0?0:g[k->index]*k->scale)+(uint32_t)k->displacement;}
static void prepare(_cpu *c,const Case *k,unsigned rhs){
 for(unsigned i=0;i<sizeof *c;i+=4){uint32_t n=random_word();memcpy((char *)c+i,&n,4);}
 uint32_t g[8];memcpy(g,c,32);g[k->source]=(g[k->source]&~(255u<<k->shift))|(rhs<<k->shift);memcpy(c,g,32);
}
int main(void){
 _Static_assert(sizeof(_cpu)==440,"CPU ABI");unsigned long long count=0;
 for(unsigned k=0;k<sizeof cases/sizeof cases[0];k++)for(unsigned n=0;n<65536;n++){
  unsigned lhs=n>>8,rhs=n&255;_cpu cpu,expected;unsigned char bytes[65],saved[65];
  prepare(&cpu,cases+k,rhs);for(unsigned j=0;j<sizeof bytes;j++)bytes[j]=(unsigned char)random_word();bytes[32]=(unsigned char)lhs;memcpy(saved,bytes,sizeof bytes);
  cpu._pointer_offset=(uintptr_t)(bytes+32)-address(&cpu,cases+k);expected=cpu;
  expected._eflags=(cpu._eflags&~0x8d5u)|subtract_flags(lhs,rhs);
  bodies[k](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(bytes,saved,sizeof bytes));count++;
 }
 long page=sysconf(_SC_PAGESIZE);assert(page>0);unsigned char *guard=mmap(NULL,(size_t)page*3,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED&&!mprotect(guard+page,(size_t)page,PROT_READ|PROT_WRITE));
 unsigned boundary_reads=0;
 for(unsigned k=0;k<sizeof cases/sizeof cases[0];k++)for(unsigned side=0;side<2;side++){
  _cpu cpu,expected;prepare(&cpu,cases+k,127);unsigned char *byte=guard+page+(side?page-1:0);*byte=128;
  cpu._pointer_offset=(uintptr_t)byte-address(&cpu,cases+k);expected=cpu;expected._eflags=(cpu._eflags&~0x8d5u)|subtract_flags(128,127);
  bodies[k](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&*byte==128);boundary_reads++;
 }
 assert(!munmap(guard,(size_t)page*3));printf("PASS: %llu exhaustive emitted byte comparisons, %u exact-byte page-boundary reads; full440CPU and memory preserved\n",count,boundary_reads);return 0;
}
