#include "../port/llasm-support/llasm_cpu.h"
#include "softfloat.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
typedef struct {unsigned op;uint64_t left,right;uint32_t control,result,raised,same_result,same_raised;} Case;
#include "cases.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
static uint32_t state=0x13b754ef;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
 unsigned long long count=0;
 for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<512;iteration++) {
  const move_case *c=&moves[block];const Case *test=&cases[offsets[c->op]+((block*512+iteration)*19)%counts[c->op]];
  _cpu cpu,expected;unsigned char backing[97],wanted[97],merge[16];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=test->control;cpu._pointer_offset=(uintptr_t)memory-0x200000;
  unsigned n=c->op==2?8:4,offset=c->address==3?3:iteration%(65-n);
  unsigned left=c->op==2?c->dest:c->merge;
  memcpy(cpu._xmm[left],&test->left,n);
  if(c->kind) {
   memcpy(memory+offset,&test->right,n);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-16;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
   else if(c->address==4)cpu._ebp=0x200000+offset-16;
  } else memcpy(cpu._xmm[c->source],&test->right,n);
  unsigned alias=!c->kind&&left==c->source,result=alias?test->same_result:test->result,raised=alias?test->same_raised:test->raised;
  expected=cpu;memcpy(wanted,backing,sizeof wanted);expected._mxcsr|=raised;
  if(c->op==2)expected._eflags=(expected._eflags&~0x8d5u)|result;
  else {
   memcpy(merge,cpu._xmm[c->merge],16);memcpy(merge,&result,4);memcpy(expected._xmm[c->dest],merge,16);memset(expected._ymm_hi[c->dest],0,16);
  }
  softfloat_roundingMode=iteration%4;softfloat_detectTininess=iteration&1;softfloat_exceptionFlags=iteration&31;extF80_roundingPrecision=(uint_fast8_t[]){32,64,80}[iteration%3];
  unsigned round=softfloat_roundingMode,tiny=softfloat_detectTininess,flags=softfloat_exceptionFlags,precision=extF80_roundingPrecision;
  bodies[block](&cpu);
  if(memcmp(&cpu,&expected,sizeof cpu)||memcmp(backing,wanted,sizeof backing)){fprintf(stderr,"scalar selection block %u iteration %u op %u alias %u result %x status %x\n",block,iteration,c->op,alias,result,raised);assert(0);}
  assert(round==softfloat_roundingMode&&tiny==softfloat_detectTininess&&flags==softfloat_exceptionFlags&&precision==extF80_roundingPrecision);count++;
 }
 printf("PASS: %llu strict emitted MINSS/MAXSS/COMISD cases; all16 original sites, C5/C4 WIG aliases/addressing, exact rational candidates, full CPU/vector/flags/MXCSR/guarded memory and TLS preservation\n",count);return 0;
}
