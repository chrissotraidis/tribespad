#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cases.inc"
#include "moves.inc"
int main(void) {
 unsigned char *arena=malloc(0x220100);assert(arena);unsigned long total=0;
 for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r) {
  const move_case *m=&moves[r];unsigned count=0;for(unsigned j=0;j<sizeof cases/sizeof cases[0];++j)if(cases[j].op==m->op)++count;
  for(unsigned iteration=0;iteration<(r<7?count:16);++iteration) {
   unsigned selected=r<7?iteration:(iteration*197+r*13)%count,index=0,c=0;
   for(;c<sizeof cases/sizeof cases[0];++c)if(cases[c].op==m->op&&index++==selected)break;assert(c<sizeof cases/sizeof cases[0]);
   const struct conversion_case *v=&cases[c];unsigned char *base=arena+(iteration%16);
   _cpu cpu;memset(&cpu,(unsigned char)(r+iteration),sizeof cpu);cpu._pointer_offset=(uintptr_t)base;cpu._mxcsr=v->control;
   cpu._eax=0x1003;cpu._esp=0x1100;cpu._ebx=0x1400;cpu._esi=0x11;
   uint32_t address=m->address==0?cpu._eax:m->address==1?cpu._esp:m->address==2?cpu._ebx+cpu._esi*4:m->address==3?0x200003:cpu._eax;
   address+=m->disp;unsigned char saved[80];
   if(m->kind){memset(base+address-16,0xa6,80);memcpy(base+address,v->input,m->op<2?8:16);memcpy(saved,base+address-16,80);}
   else memcpy(cpu._xmm[m->source],v->input,16);
   _cpu wanted=cpu;wanted._mxcsr|=v->raised;memcpy(wanted._xmm[m->dest],v->output,16);memset(wanted._ymm_hi[m->dest],0,16);
   bodies[r](&cpu);assert(!memcmp(&cpu,&wanted,sizeof cpu));if(m->kind)assert(!memcmp(saved,base+address-16,80));++total;
  }
 }
 free(arena);printf("PASS: %lu emitted quadword conversion checks, full CPU and source guards\n",total);return 0;
}
