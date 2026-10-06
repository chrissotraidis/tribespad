#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cases.inc"
_Static_assert(sizeof(_cpu)==440,"CPU ABI");
static uint32_t state=0x93ae017b;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static uint32_t flags(uint32_t a,uint32_t b){
 uint32_t result=a-b,low=result&255,ones=0;
 for(unsigned bit=0;bit<8;bit++)ones+=(low>>bit)&1;
 return (a<b?1:0)|(!(ones&1)?4:0)|((a^b^result)&16)|(result==0?64:0)|((result>>24)&128)|((((a^b)&(a^result))>>20)&2048);
}
int main(void){
 unsigned long long count=0;
 for(unsigned block=0;block<sizeof right/sizeof right[0];block++)for(unsigned i=0;i<65536;i++){
  _cpu cpu,expected;unsigned char backing[97],wanted[97];
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  uint32_t value=(uint32_t[]){0,1,UINT32_MAX,0x80000000,0x7fffffff,0xfffa0000,0xfffe1000,0x55555555,0xaaaaaaaa,right[block],right[block]-1,right[block]+1}[i%12];
  if(i>=1024 && i%12>=9)value=random_word();
  memcpy(backing+17+slot_index[block]*4,&value,4);memcpy(wanted,backing,sizeof wanted);
  cpu._pointer_offset=(uintptr_t)(backing+17)-0x101296a0;cpu._mxcsr=i;
  unsigned stack_offset=i%61;cpu._esp=0x101296a0+stack_offset+4;expected=cpu;
  if(kind[block]){expected._esp-=4;memcpy(wanted+17+stack_offset,&value,4);}
  else expected._eflags=(cpu._eflags&~0x8d5u)|flags(value,right[block]);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));count++;
 }
 printf("PASS: %llu emitted import-slot CMP/PUSH cases; three original dbghelp sites/imports/relocations, mutable zero/token values, signed8/full32 immediates, subtraction flags, exact stack writes including overlap, fullCPU/allMXCSR/unaligned memory guards\n",count);return 0;
}
