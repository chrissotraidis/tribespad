#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"FullCPU ABI");
static uint32_t state=0x25034577;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void){
 uint64_t count=0;
 for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<128;iteration++){
  const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],a[32],b[16],value[32],av[32],bv[16];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<97;j++)backing[j]=(unsigned char)random_word();cpu._mxcsr=(block*128+iteration)&0xffff;cpu._pointer_offset=(uintptr_t)memory-0x200000;unsigned offset=c->address==3?3:iteration%49;
  for(unsigned j=0;j<32;j++)av[j]=iteration<64?(unsigned char)(j+block):(unsigned char)random_word();
  for(unsigned j=0;j<16;j++)bv[j]=iteration<64?(unsigned char)(255-j-block):(unsigned char)random_word();
  memcpy(cpu._xmm[c->merge],av,16);memcpy(cpu._ymm_hi[c->merge],av+16,16);
  if(c->kind){
   memcpy(memory+offset,bv,16);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-c->disp;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
   memcpy(b,memory+offset,16);
  }else memcpy(cpu._xmm[c->source],bv,16);
  memcpy(a,cpu._xmm[c->merge],16);memcpy(a+16,cpu._ymm_hi[c->merge],16);
  if(!c->kind)memcpy(b,cpu._xmm[c->source],16);
  for(unsigned j=0;j<32;j++)value[j]=j/16==(c->control%2)?b[j%16]:a[j];
  expected=cpu;memcpy(wanted,backing,97);memcpy(expected._xmm[c->dest],value,16);memcpy(expected._ymm_hi[c->dest],value+16,16);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,97));count++;
 }
 printf("PASS: %llu emitted VINSERTI128 cases; all3 original sites/all register aliases/allimm8/half preservation, exactguardedmemory/fullCPU/flags/allMXCSR\n",(unsigned long long)count);return 0;
}
