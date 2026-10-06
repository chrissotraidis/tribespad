#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
int main(void){unsigned long total=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r?512:65536);++i){const move_case *v=&moves[r];_cpu c,expected;uint32_t seed=0xa0d+r*13+i;for(unsigned k=0;k<sizeof c;++k)((unsigned char *)&c)[k]=(unsigned char)rnd(&seed);c._mxcsr=i&65535;
if(i<128){uint32_t a[4],b[4];for(unsigned k=0;k<4;++k){a[k]=k==i/32?1u<<(i%32):0;b[k]=~a[k];}memcpy(c._xmm[v->dest],a,16);if(v->dest!=v->source)memcpy(c._xmm[v->source],b,16);}
expected=c;unsigned char value[8];memcpy(value,c._xmm[v->source]+(v->high?0:8),8);memcpy(expected._xmm[v->dest]+(v->high?8:0),value,8);bodies[r](&c);assert(!memcmp(&c,&expected,sizeof c));++total;}
printf("PASS: %lu emitted legacy MOVHLPS/MOVLHPS checks; full CPU preservation\n",total);return 0;}
