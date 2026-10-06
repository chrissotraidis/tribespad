#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"Full CPU ABI");
static uint32_t state=0x13b754ef;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
 unsigned long long count=0;
 for(unsigned block=0;block<sizeof cases/sizeof cases[0];block++)for(unsigned iteration=0;iteration<512;iteration++) {
  const move_case *c=&cases[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],value[8];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=(block*512+iteration)&65535;cpu._pointer_offset=(uintptr_t)memory-0x200000;
  unsigned n=c->quad?8:4,offset=c->address==3?3:iteration%(65-n);
  uint64_t bits=(uint64_t[]){0,UINT64_MAX,1,UINT64_C(0x8000000000000000),UINT64_C(0x7ff0000000000000),UINT64_C(0xfff0000000000000),UINT64_C(0x7ff012345678abcd),UINT64_C(0xfff812345678abcd),UINT64_C(0x000fffffffffffff),UINT64_C(0x0010000000000000),UINT64_C(0x7f81234580000001),UINT64_C(0x55555555aaaaaaaa)}[iteration%12];
  if(iteration>=64)bits=((uint64_t)random_word()<<32)|random_word();
  if(c->kind==3)memcpy((unsigned char *)&cpu+c->source*4,&bits,4);else memcpy(cpu._xmm[c->source],&bits,n);
  if(c->kind==1||c->kind==2) {
   if(c->kind==1)memcpy(memory+offset,&bits,n);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-c->disp;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
   else if(c->address==4)cpu._ebp=0x200000+offset-c->disp;
  }
  expected=cpu;memcpy(wanted,backing,sizeof wanted);
  if(c->kind==3)memcpy(value,(unsigned char *)&cpu+c->source*4,4);
  else if(c->kind==1)memcpy(value,memory+offset,n);
  else memcpy(value,cpu._xmm[c->source],n);
  if(c->kind==2)memcpy(wanted+17+offset,value,n);
  else if(c->kind==4)memcpy((unsigned char *)&expected+c->dest*4,value,4);
  else {memset(expected._xmm[c->dest],0,16);memcpy(expected._xmm[c->dest],value,n);memset(expected._ymm_hi[c->dest],0,16);}
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));count++;
 }
 printf("PASS: %llu strict emitted VMOVD/VMOVQ cases; all77 original sites, both directions/encodings/GPR/XMM roles, upper clearing and store preservation, full CPU/flags/allMXCSR/guarded exact4/8-byte memory\n",count);return 0;
}
