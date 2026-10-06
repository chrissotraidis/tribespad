#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "broadcast_bodies.inc"
_Static_assert(sizeof(_cpu)==440,"FullCPU ABI");
static uint32_t state=0x250349ab;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void){
 uint64_t count=0;unsigned char seen[8192]={0};
 for(unsigned block=0;block<sizeof cases/sizeof cases[0];block++)for(unsigned iteration=0;iteration<1024;iteration++){
  const broadcast_case *c=&cases[block];_cpu cpu,expected;unsigned char backing[97],before[97],value[16],candidate[32];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=(block*1024+iteration)&0xffff;cpu._pointer_offset=(uintptr_t)memory-0x200000;
  unsigned n=2u<<c->kind,offset=c->address==3?3:iteration%(65-n);
  for(unsigned j=0;j<16;j++)value[j]=(unsigned char)random_word();
  if(c->kind==0){uint16_t word=(block*1024+iteration)&0xffff;memcpy(value,&word,2);seen[word/8]|=(unsigned char)(1u<<(word%8));}
  else if(iteration<16){uint64_t bits=(uint64_t[]){0,1,UINT64_MAX,UINT64_C(0x8000000000000000),UINT64_C(0xffffffff00000000),UINT64_C(0x0123456789abcdef),UINT64_C(0x7fffffffffffffff),UINT64_C(0x80000001)}[iteration%8];memcpy(value,&bits,n<8?n:8);}
  if(c->memory){
   memcpy(memory+offset,value,n);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-12;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
   else if(c->address==4)cpu._edx=0x200000+offset;
   memcpy(value,memory+offset,n);
  }else{memcpy(cpu._xmm[c->source],value,n);memcpy(value,cpu._xmm[c->source],n);}
  expected=cpu;memcpy(before,backing,sizeof before);
  for(unsigned j=0;j<(c->wide?32:16);j++)candidate[j]=value[j%n];
  memcpy(expected._xmm[c->dest],candidate,16);
  if(c->wide)memcpy(expected._ymm_hi[c->dest],candidate+16,16);else memset(expected._ymm_hi[c->dest],0,16);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,before,sizeof backing));count++;
 }
 for(unsigned i=0;i<8192;i++)assert(seen[i]==255);
 printf("PASS: %llu strict emitted integer broadcast cases; all10 original sites, all65536wordvalues acrossprofile, aliases/bothwidths/exact2/4/8/16-byte guardedmemory, fullCPU/flags/allMXCSR\n",(unsigned long long)count);return 0;
}
