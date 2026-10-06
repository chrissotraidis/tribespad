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
 for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<(block==8?65536:256);iteration++){
  const move_case *c=&moves[block];_cpu cpu,expected;unsigned char source[32];
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();cpu._mxcsr=(block*256+iteration)&0xffff;
  for(unsigned j=0;j<32;j++)source[j]=block==8?((random_word()&127)|(((iteration>>(j%16))&1)*128)):(unsigned char)(iteration+j*17);
  memcpy(cpu._xmm[c->source],source,16);memcpy(cpu._ymm_hi[c->source],source+16,16);
  unsigned n=c->wide?32:16;uint32_t value=0;
  for(unsigned j=n;j>0;j--)value=value*2+(source[j-1]>=128);
  for(unsigned j=0;j<n;j++)seen[j][source[j]/8]|=(unsigned char)(1u<<(source[j]%8));
  expected=cpu;memcpy((unsigned char *)&expected+c->dest*4,&value,4);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu));count++;
 }
 for(unsigned j=0;j<32;j++)for(unsigned k=0;k<32;k++)assert(seen[j][k]==255);
 printf("PASS: %llu emitted VPMOVMSKB cases; 8originalsites/allGPRandvectorroles/bothwidths/C5C4WIG/every16bitmask/allbytevaluesperposition/fullCPU/allflags/allMXCSR\n",(unsigned long long)count);return 0;
}
