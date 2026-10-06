#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cases.inc"
_Static_assert(sizeof(_cpu)==440,"CPU ABI");
static uint32_t state=0x48e591ad;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void){
 unsigned long long count=0;
 for(unsigned block=0;block<sizeof cases/sizeof cases[0];block++)for(unsigned iteration=0;iteration<128;iteration++){
  const shift_case *c=&cases[block];_cpu cpu,expected;unsigned char backing[97],before[97],a[32],b[32],value[32]={0};unsigned n=c->wide?32:16,cn=c->op==4?n:16;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=(block*128+iteration)&65535;cpu._pointer_offset=(uintptr_t)(backing+17)-0x200000;
  for(unsigned j=0;j<32;j++){a[j]=iteration<32?(unsigned char)(j+iteration*17):(unsigned char)random_word();b[j]=(unsigned char)random_word();}
  memcpy(cpu._xmm[c->source],a,16);if(c->wide)memcpy(cpu._ymm_hi[c->source],a+16,16);
  uint64_t scalar=(uint64_t[]){0,1,15,16,31,32,63,64,65,255,256,UINT32_MAX,UINT64_C(0x100000000),UINT64_MAX}[iteration%14];memcpy(b,&scalar,8);
  if(c->op==4)for(unsigned j=0;j<cn;j+=4){uint32_t lane=(uint32_t[]){0,1,15,16,30,31,32,33,63,64,255,256,0x80000000,UINT32_MAX}[(iteration+j/4)%14];memcpy(b+j,&lane,4);}
  unsigned offset=c->address==3?3:iteration%(65-cn);
  if(c->kind==1){memcpy(cpu._xmm[c->countreg],b,16);if(c->op==4&&c->wide)memcpy(cpu._ymm_hi[c->countreg],b+16,16);}
  else if(c->kind==2){memcpy(backing+17+offset,b,cn);if(c->address==0)cpu._eax=0x200000+offset;else if(c->address==1)cpu._esp=0x200000+offset-16;else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;}
  memcpy(a,cpu._xmm[c->source],16);if(c->wide)memcpy(a+16,cpu._ymm_hi[c->source],16);
  if(c->kind==1){memcpy(b,cpu._xmm[c->countreg],16);if(c->op==4&&c->wide)memcpy(b+16,cpu._ymm_hi[c->countreg],16);}else if(c->kind==2)memcpy(b,backing+17+offset,cn);
  if(c->kind)memcpy(&scalar,b,8);else scalar=c->count;
  if(c->op==0||c->op==4)for(unsigned j=0;j<n;j+=4){uint32_t x,k;memcpy(&x,a+j,4);if(c->op==4){memcpy(&k,b+j,4);scalar=k;}uint32_t result=scalar<32?(uint32_t)((uint64_t)x*(UINT64_C(1)<<(unsigned)scalar)):0;memcpy(value+j,&result,4);}
  else if(c->op==1)for(unsigned j=0;j<n;j+=8){uint64_t x;memcpy(&x,a+j,8);uint64_t result=scalar<64?x/(UINT64_C(1)<<(unsigned)scalar):0;memcpy(value+j,&result,8);}
  else if(scalar<16)for(unsigned j=0;j<n;j+=16){if(c->op==2)memmove(value+j+(unsigned)scalar,a+j,16-(unsigned)scalar);else memmove(value+j,a+j+(unsigned)scalar,16-(unsigned)scalar);}
  expected=cpu;memcpy(before,backing,sizeof before);memcpy(expected._xmm[c->dest],value,16);if(c->wide)memcpy(expected._ymm_hi[c->dest],value+16,16);else memset(expected._ymm_hi[c->dest],0,16);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,before,sizeof backing));count++;
 }
 printf("PASS: %llu emitted packed shift cases; all22 original sites, all immediates/canonical roles, boundary counts/all aliases, scalar64/per-dword counts, independent128-bit-lane shifts, fullCPU/flags/allMXCSR/guarded memory\n",count);return 0;
}
