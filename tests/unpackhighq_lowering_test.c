#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
static uint32_t state=0x13b754ef;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
 unsigned long long count=0;
 for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<128;iteration++) {
  const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],a[32],b[32],result[32];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=(block*128+iteration)&0xffff;unsigned n=c->wide?32:16,offset=c->address==3?3:c->address==5?0:iteration%(65-n);
  unsigned char av[32],bv[32];
  for(unsigned j=0;j<32;j++) {
   av[j]=iteration<32?(unsigned char)(j+iteration*31):(unsigned char)random_word();
   bv[j]=iteration<32?(unsigned char)(255-j-iteration*17):(unsigned char)random_word();
  }
  memcpy(cpu._xmm[c->left],av,16);if(c->wide)memcpy(cpu._ymm_hi[c->left],av+16,16);
  cpu._pointer_offset=(uintptr_t)memory-0x200000;
  if(c->kind) {
   memcpy(memory+offset,bv,n);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-c->disp;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
   else if(c->address==4)cpu._ecx=0x200000+offset;
   else if(c->address==5)cpu._pointer_offset=(uintptr_t)memory-c->disp;
   memcpy(b,memory+offset,n);
  } else {
   memcpy(cpu._xmm[c->right],bv,16);if(c->wide)memcpy(cpu._ymm_hi[c->right],bv+16,16);
   memcpy(b,cpu._xmm[c->right],16);if(c->wide)memcpy(b+16,cpu._ymm_hi[c->right],16);
  }
  memcpy(a,cpu._xmm[c->left],16);if(c->wide)memcpy(a+16,cpu._ymm_hi[c->left],16);
  for(unsigned j=0;j<n;j++)result[j]=j%16<8?a[j-j%16+8+j%8]:b[j-j%16+8+j%8];
  expected=cpu;memcpy(wanted,backing,sizeof wanted);memcpy(expected._xmm[c->dest],result,16);
  if(c->wide)memcpy(expected._ymm_hi[c->dest],result+16,16);else memset(expected._ymm_hi[c->dest],0,16);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));count++;
 }
 printf("PASS: %llu strict emitted high-qword unpack cases; all8 original sites, independent128-bit lane interleave, aliases/widths/C5/C4 WIG, exact guarded unaligned memory, fullCPU/flags/allMXCSR\n",count);return 0;
}
