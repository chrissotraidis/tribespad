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
 uint64_t count=0;unsigned char seen[32][32]={{0}};
 for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<256;iteration++){
  const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],a[32],b[32],mask[32],value[32],av[32],bv[32],mv[32];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<97;j++)backing[j]=(unsigned char)random_word();cpu._mxcsr=(block*256+iteration)&0xffff;cpu._pointer_offset=(uintptr_t)memory-0x200000;unsigned n=c->wide?32:16,offset=c->address==3?3:iteration%(65-n);
  for(unsigned j=0;j<32;j++){av[j]=iteration<128?(unsigned char)(j+block):(unsigned char)random_word();bv[j]=iteration<128?(unsigned char)(255-j-block):(unsigned char)random_word();mv[j]=(unsigned char)(iteration+j*17);}
  memcpy(cpu._xmm[c->left],av,16);if(c->wide)memcpy(cpu._ymm_hi[c->left],av+16,16);
  if(c->kind){
   memcpy(memory+offset,bv,n);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-c->disp;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
   memcpy(b,memory+offset,n);
  }else{memcpy(cpu._xmm[c->right],bv,16);if(c->wide)memcpy(cpu._ymm_hi[c->right],bv+16,16);}
  memcpy(cpu._xmm[c->mask],mv,16);if(c->wide)memcpy(cpu._ymm_hi[c->mask],mv+16,16);
  memcpy(a,cpu._xmm[c->left],16);memcpy(mask,cpu._xmm[c->mask],16);if(c->wide){memcpy(a+16,cpu._ymm_hi[c->left],16);memcpy(mask+16,cpu._ymm_hi[c->mask],16);}
  if(!c->kind){memcpy(b,cpu._xmm[c->right],16);if(c->wide)memcpy(b+16,cpu._ymm_hi[c->right],16);}
  for(unsigned j=0;j<n;j++){seen[j][mask[j]/8]|=(unsigned char)(1u<<(mask[j]%8));value[j]=mask[j]>=128?b[j]:a[j];}
  expected=cpu;memcpy(wanted,backing,97);memcpy(expected._xmm[c->dest],value,16);if(c->wide)memcpy(expected._ymm_hi[c->dest],value+16,16);else memset(expected._ymm_hi[c->dest],0,16);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,97));count++;
 }
 for(unsigned j=0;j<32;j++)for(unsigned k=0;k<32;k++)assert(seen[j][k]==255);
 printf("PASS: %llu emitted VPBLENDVB cases; originalsite/all4-role aliases/bothwidths, allmaskbytevalues perposition/allis4 bytes IA32ignoredhighbit/lownibble, exactguardedmemory/fullCPU/flags/allMXCSR\n",(unsigned long long)count);return 0;
}
