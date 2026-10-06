#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "broadcast_bodies.inc"
static uint32_t state=0x47b123ad;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void){
 uint64_t count=0;
 for(unsigned block=0;block<sizeof cases/sizeof cases[0];block++)for(unsigned iteration=0;iteration<2048;iteration++){
  const broadcast_case *c=&cases[block];_cpu cpu,expected;unsigned char backing[97],before[97],value[16],candidate[32];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=iteration<64?iteration:random_word()&0xffff;
  cpu._pointer_offset=(uintptr_t)memory-0x200000;
  unsigned n=4u<<c->kind,offset=c->address==3?3:iteration%(65-n);
  uint32_t bits=(uint32_t[]){0,0x80000000,1,0x80000001,0x7f800000,0xff800000,0x7fa12345,0xffa56789,0x7fc12345,0xffc56789,0x007fffff,0x00800000,0xffffffff,0x01234567,0x89abcdef,0x3f800000}[iteration%16];
  if(c->memory){
   for(unsigned j=0;j<n;j+=4){uint32_t lane=bits^(j?random_word():0);memcpy(memory+offset+j,&lane,4);}
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-12;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
   memcpy(value,memory+offset,n);
  }else{
   memcpy(cpu._xmm[c->source],&bits,4);
   if(n==8){uint32_t high=random_word();memcpy(cpu._xmm[c->source]+4,&high,4);}
   memcpy(value,cpu._xmm[c->source],n);
  }
  expected=cpu;memcpy(before,backing,sizeof before);
  for(unsigned j=0;j<(c->wide?32:16);j+=n)memcpy(candidate+j,value,n);
  memcpy(expected._xmm[c->dest],candidate,16);
  if(c->wide)memcpy(expected._ymm_hi[c->dest],candidate+16,16);else memset(expected._ymm_hi[c->dest],0,16);
  bodies[block](&cpu);
  assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,before,sizeof backing));count++;
 }
 printf("PASS: %llu strict emitted broadcast cases; full CPU/MXCSR/flags, aliases, upper halves, guarded unaligned memory and copied NaN/denormal bits\n",(unsigned long long)count);return 0;
}
