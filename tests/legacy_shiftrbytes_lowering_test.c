#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
int main(void){unsigned long total=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r<3?65536:256);++i){const move_case *v=&moves[r];_cpu c,expected;uint32_t seed=0x5df+r*13+i;for(unsigned k=0;k<sizeof c;++k)((unsigned char *)&c)[k]=(unsigned char)rnd(&seed);c._mxcsr=i&65535;c._pointer_offset=UINT64_C(0xdead000000000000);for(unsigned k=0;k<16;++k)c._xmm[v->dest][k]=r<3?(unsigned char)(i+k*17):i<128?(k==i/8?(1u<<(i%8)):0):(unsigned char)(i*k+v->count+0x55);expected=c;for(unsigned k=0;k<16;++k)expected._xmm[v->dest][k]=k+v->count<16?c._xmm[v->dest][k+v->count]:0;bodies[r](&c);assert(!memcmp(&c,&expected,sizeof c));++total;}
printf("PASS: %lu emitted legacy PSRLDQ checks; all256 counts, full CPU preservation\n",total);return 0;}
