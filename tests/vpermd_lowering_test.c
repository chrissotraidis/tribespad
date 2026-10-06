#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"Full CPU ABI");
static uint32_t state=0x25034071;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static void permutation(unsigned rank,unsigned out[8]) {
 unsigned pool[8]={0,1,2,3,4,5,6,7},factorial[8]={1,1,2,6,24,120,720,5040};
 for(unsigned i=0;i<8;i++){unsigned position=rank/factorial[7-i];rank%=factorial[7-i];out[i]=pool[position];memmove(pool+position,pool+position+1,(7-i-position)*sizeof *pool);}
}
int main(void) {
 unsigned long long count=0;
 for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<(block?1024:40320);iteration++) {
  const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],a[32],b[32],result[32];unsigned char *memory=backing+17;unsigned select[8];
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=(block*1024+iteration)&0xffff;unsigned offset=c->address==3?3:iteration%33;
  if(!block)permutation(iteration,select);
  else for(unsigned j=0;j<8;j++)select[j]=iteration<8?iteration:iteration<16?(j+iteration)&7:iteration<24?(7-j+iteration)&7:random_word()&7;
  uint32_t indices[8],data[8];
  for(unsigned j=0;j<8;j++){indices[j]=(random_word()&~7u)|select[j];data[j]=iteration<32?(0x01020304u*(j+1)):random_word();}
  memcpy(cpu._xmm[c->left],indices,16);memcpy(cpu._ymm_hi[c->left],(unsigned char *)indices+16,16);cpu._pointer_offset=(uintptr_t)memory-0x200000;
  if(c->kind) {
   memcpy(memory+offset,data,32);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-c->disp;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
   memcpy(b,memory+offset,32);
  } else {memcpy(cpu._xmm[c->right],data,16);memcpy(cpu._ymm_hi[c->right],(unsigned char *)data+16,16);memcpy(b,cpu._xmm[c->right],16);memcpy(b+16,cpu._ymm_hi[c->right],16);}
  memcpy(a,cpu._xmm[c->left],16);memcpy(a+16,cpu._ymm_hi[c->left],16);
  for(unsigned j=0;j<8;j++){uint32_t index;memcpy(&index,a+j*4,4);unsigned k=index%8;for(unsigned byte=0;byte<4;byte++)result[j*4+byte]=b[k*4+byte];}
  expected=cpu;memcpy(wanted,backing,sizeof wanted);memcpy(expected._xmm[c->dest],result,16);memcpy(expected._ymm_hi[c->dest],result+16,16);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));count++;
 }
 printf("PASS: %llu emitted VPERMD cases; both original sites, all aliases, cross128-bit lanes, all40320 permutations on original source-destination alias, index high bits/repeats, exact32-byte guarded unaligned memory, fullCPU/flags/allMXCSR\n",count);return 0;
}
