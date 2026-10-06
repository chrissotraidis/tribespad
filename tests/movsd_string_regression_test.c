#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t form,source,dest,count,flags,expected_esi,expected_edi,expected_ecx,expected_eflags;unsigned char after[1024];} Case;
#include "cases.inc"
extern void test_0(_cpu *),test_1(_cpu *);
int main(void){for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++){
 const Case *t=&cases[i];unsigned char bytes[1024];for(unsigned n=0;n<sizeof bytes;n++)bytes[n]=(n*71+23)&255;
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)bytes-0x2000;cpu._esi=t->source;cpu._edi=t->dest;cpu._ecx=t->count;cpu._eflags=t->flags;
 _cpu expected=cpu;expected._esi=t->expected_esi;expected._edi=t->expected_edi;expected._ecx=t->expected_ecx;expected._eflags=t->expected_eflags;
 (t->form?test_1:test_0)(&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(bytes,t->after,sizeof bytes));
}printf("PASS: %zu emitted MOVSD/string-REP x86 comparisons, both directions, zero counts, overlap, alignment and full CPU preservation\n",sizeof cases/sizeof cases[0]);}
