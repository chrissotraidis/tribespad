#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static uint32_t get(const _cpu *c,unsigned i){uint32_t v;memcpy(&v,(const unsigned char *)c+i*4,4);return v;}
static void put(_cpu *c,unsigned i,uint32_t v){memcpy((unsigned char *)c+i*4,&v,4);}
static uint32_t cmp(uint32_t x,uint32_t y){uint32_t d=x-y;return (x<y)|((__builtin_parity(d&255)^1)<<2)|((x^y^d)&16)|((d==0)<<6)|((d>>31)<<7)|((((x^y)&(x^d))>>31)<<11);}
int main(void){unsigned long total=0;const uint32_t high[]={0,32,0x80000000u,0xffffffe0u,0x10000,0x12345660,0x7fffffe0,0xffffffc0};const uint32_t values[]={0,1,0x80000000u,0x7fffffffu,0xffffffffu,0x100,0x10,0xf};
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r?256:65536);++i){const move_case *v=&moves[r];_cpu c,expected;uint32_t seed=0xb750+r*13+i;for(unsigned k=0;k<sizeof c;++k)((unsigned char *)&c)[k]=(unsigned char)rnd(&seed);c._mxcsr=i&65535;c._pointer_offset=UINT64_C(0xdead000000000000);unsigned pattern=r?i:i>>8;put(&c,v->dest,pattern<32?(1u<<pattern):pattern==32?0:pattern==33?UINT32_MAX:rnd(&seed));if(v->kind!=1)put(&c,v->source,high[(i>>5)&7]|(i&31));if(v->kind>=2){c._ecx=values[i&7];c._ebx=values[(i>>3)&7];}expected=c;if(v->kind>=2)expected._eflags=(c._eflags&~0x8d5u)|cmp(c._ecx,c._ebx);unsigned bit=(v->kind==1?v->count:get(&c,v->source))&31;uint32_t old=get(&c,v->dest);put(&expected,v->dest,old|(1u<<bit));expected._eflags=(expected._eflags&~1u)|((old>>bit)&1);if(v->kind>=2){expected._ebx=(expected._ebx&~255u)|((expected._eflags>>6)&1);expected._edx=(expected._edx&~255u)|(expected._eflags&1);}bodies[r](&c);if(v->kind==3){c._eflags&=~0x894u;expected._eflags&=~0x894u;}assert(!memcmp(&c,&expected,sizeof c));++total;}
printf("PASS: %lu emitted BTS checks; REG/IMM aliases, preserved ZF and full CPU policy\n",total);return 0;}
