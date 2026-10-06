#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "moves.inc"
static uint32_t random_word(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static void prepare(_cpu *cpu,unsigned body,unsigned iteration) {
 uint32_t state=0x6432128u+body*13+iteration;for(unsigned j=0;j<sizeof *cpu;++j)((unsigned char *)cpu)[j]=(unsigned char)random_word(&state);
 cpu->_mxcsr=(0xffffu-body*16-iteration)&0xffff;cpu->_eax=0x1003;cpu->_esp=0x1100;cpu->_ebx=0x1400;cpu->_esi=0x11;
}
static uint32_t address(_cpu *cpu,const move_case *v){return v->address==0?cpu->_eax:v->address==1?cpu->_esp+16:v->address==2?cpu->_ebx+cpu->_esi*4-17:0x200003;}
static void oracle(_cpu *wanted,unsigned char *memory,const move_case *v) {
 uint32_t value;if(v->op) {
  if(v->kind)memcpy(&value,memory,4);else value=((uint32_t *)wanted)[v->source];
  unsigned char result[16];memcpy(result,wanted->_xmm[v->merge],16);memcpy(result+(v->control%4)*4,&value,4);
  memcpy(wanted->_xmm[v->dest],result,16);memset(wanted->_ymm_hi[v->dest],0,16);
 } else {
  memcpy(&value,wanted->_xmm[v->source]+(v->control%4)*4,4);
  if(v->kind)memcpy(memory,&value,4);else ((uint32_t *)wanted)[v->dest]=value;
 }
}
int main(void) {
 unsigned char *arena=malloc(0x220100);assert(arena);unsigned long total=0;unsigned protected_count=0;
 for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned iteration=0;iteration<(r<9?128:16);++iteration) {
  const move_case *v=&moves[r];_cpu cpu;prepare(&cpu,r,iteration);cpu._pointer_offset=(uintptr_t)(arena+iteration%16);unsigned char *base=(void *)(uintptr_t)cpu._pointer_offset;
  if(r<9){memset(cpu._xmm[v->op?v->merge:v->source],0,16);cpu._xmm[v->op?v->merge:v->source][iteration/8]=(unsigned char)(1u<<(iteration%8));if(v->op&&!v->kind)((uint32_t *)&cpu)[v->source]=1u<<(iteration%32);}
  uint32_t offset=address(&cpu,v);unsigned char saved[80],wanted_memory[80];if(v->kind){memset(base+offset-16,0xa6,80);uint32_t word=0x12345678u+iteration;memcpy(base+offset,&word,4);memcpy(saved,base+offset-16,80);memcpy(wanted_memory,saved,80);}
  _cpu wanted=cpu;oracle(&wanted,v->kind?wanted_memory+16:NULL,v);bodies[r](&cpu);assert(!memcmp(&cpu,&wanted,sizeof cpu));if(v->kind)assert(!memcmp(base+offset-16,wanted_memory,80));++total;
 }
 size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));unsigned char *snapshot=malloc(page);assert(snapshot);
 for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r) {
  const move_case *v=&moves[r];if(!v->kind)continue;_cpu cpu;prepare(&cpu,r,0);uint32_t offset=address(&cpu,v);cpu._pointer_offset=(uintptr_t)(guard+page-4)-offset;
  memset(guard,0xa6,page);uint32_t initial=0x9abcdef0;memcpy(guard+page-4,&initial,4);memcpy(snapshot,guard,page);_cpu wanted=cpu;oracle(&wanted,snapshot+page-4,v);
  bodies[r](&cpu);assert(!memcmp(&cpu,&wanted,sizeof cpu));assert(!memcmp(guard,snapshot,page));++protected_count;++total;
 }
 assert(!munmap(guard,page*2));free(snapshot);free(arena);printf("PASS: %lu emitted dword move checks, %u protected pages, full CPU and arena\n",total,protected_count);return 0;
}
