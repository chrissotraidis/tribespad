#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"FullCPU ABI");
static uint32_t state=0x25034577;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static uint32_t readreg(const _cpu *c,unsigned n){uint32_t v;memcpy(&v,(const unsigned char *)c+4*n,4);return v;}
static void writereg(_cpu *c,unsigned n,uint32_t v){memcpy((unsigned char *)c+4*n,&v,4);}
int main(void){
 uint64_t count=0;
 for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<256;iteration++){
  const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<97;j++)backing[j]=(unsigned char)random_word();cpu._mxcsr=(block*256+iteration)&0xffff;cpu._pointer_offset=(uintptr_t)memory-0x200000;unsigned offset=c->address==3?3:iteration%61;
  uint32_t value=iteration<64?(UINT32_C(1)<<(iteration%32)):random_word();
  writereg(&cpu,c->countreg,iteration<128?iteration:((random_word()&~31u)|(iteration%32)));
  if(c->kind){
   memcpy(memory+offset,&value,4);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-c->disp;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
  }else writereg(&cpu,c->source,value);
  if(!c->kind)value=readreg(&cpu,c->source);
  unsigned shift=readreg(&cpu,c->countreg)%32;uint64_t factor=UINT64_C(1)<<shift;
  uint32_t result=c->op?(uint32_t)((uint64_t)value/factor):(uint32_t)((uint64_t)value*factor);
  expected=cpu;memcpy(wanted,backing,97);writereg(&expected,c->dest,result);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,97));count++;
 }
 printf("PASS: %llu emitted SHLX/SHRX cases; 13originalsites/allGPRaliases/WIG/countmask/exactdwordguardedmemory/fullCPU/allflags/allMXCSR\n",(unsigned long long)count);return 0;
}
