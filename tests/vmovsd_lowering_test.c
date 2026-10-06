#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
static uint32_t state=0x13b754ef;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void){
 unsigned long long count=0;
 for(unsigned block=0;block<sizeof cases/sizeof cases[0];block++)for(unsigned iteration=0;iteration<256;iteration++){
  const move_case *c=&cases[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],scalar[8],merge[8];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=iteration<64?iteration:random_word()&0xffff;cpu._pointer_offset=(uintptr_t)memory-0x200000;
  uint64_t bits=(uint64_t[]){0,UINT64_C(0x8000000000000000),1,UINT64_C(0x8000000000000001),UINT64_C(0x7ff0000000000000),UINT64_C(0xfff0000000000000),UINT64_C(0x7ff0123456789abc),UINT64_C(0xfff0123456789abc),UINT64_C(0x7ff8123456789abc),UINT64_C(0xffffffffffffffff),UINT64_C(0x000fffffffffffff),UINT64_C(0x0010000000000000),UINT64_C(0x0123456789abcdef),UINT64_C(0xfedcba9876543210),UINT64_C(0x3ff0000000000000),UINT64_C(0xbff0000000000000)}[iteration%16];
  unsigned offset=c->address==3?3:iteration%57;
  memcpy(cpu._xmm[c->source],&bits,8);
  if(c->kind){
   if(c->kind==1)memcpy(memory+offset,&bits,8);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-16;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
  }
  expected=cpu;memcpy(wanted,backing,sizeof wanted);
  if(c->kind==2)memcpy(wanted+17+offset,cpu._xmm[c->source],8);
  else{
   memcpy(scalar,c->kind==1?memory+offset:cpu._xmm[c->source],8);
   if(c->kind==0)memcpy(merge,cpu._xmm[c->merge]+8,8);else memset(merge,0,8);
   memcpy(expected._xmm[c->dest],scalar,8);memcpy(expected._xmm[c->dest]+8,merge,8);memset(expected._ymm_hi[c->dest],0,16);
  }
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));count++;
 }
 printf("PASS: %llu strict emitted VMOVSD cases; full CPU/MXCSR/flags, all aliases, source merging, upper clearing and exact guarded eight-byte memory access\n",count);return 0;
}
