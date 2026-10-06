#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"Full CPU ABI");
static uint32_t state=0x25034715;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
 unsigned long long count=0;
 for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<128;iteration++) {
  const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],data[32],result[16];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=(block*128+iteration)&0xffff;unsigned offset=c->address==3?3:iteration%49;
  for(unsigned j=0;j<32;j++)data[j]=iteration<32?(unsigned char)(j+iteration*17):(unsigned char)random_word();
  memcpy(cpu._xmm[c->right],data,16);memcpy(cpu._ymm_hi[c->right],data+16,16);cpu._pointer_offset=(uintptr_t)memory-0x200000;
  if(c->kind) {
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-c->disp;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
  }
  for(unsigned j=0;j<16;j++)result[j]=data[(c->order%2)*16+j];
  expected=cpu;memcpy(wanted,backing,sizeof wanted);
  if(c->kind)memcpy(wanted+17+offset,result,16);
  else {memcpy(expected._xmm[c->dest],result,16);memset(expected._ymm_hi[c->dest],0,16);}
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));count++;
 }
 printf("PASS: %llu strict emitted VEXTRACTI128 cases; all6 original sites, everyimm8/allaliases, upperzero/exact16-byte stores, ignoredhighselectorbits, guardedunalignedmemory/fullCPU/flags/allMXCSR\n",count);return 0;
}
