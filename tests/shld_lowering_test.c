#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "moves.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static uint32_t address(const _cpu *c,const move_case *v){return v->address==0?c->_eax:v->address==1?c->_esp+16:v->address==2?c->_ebx+c->_esi*4-16:0x200010;}
static void prepare(_cpu *c,unsigned r,unsigned i){uint32_t seed=0xa403+r*17+i;for(unsigned k=0;k<sizeof *c;++k)((unsigned char *)c)[k]=(unsigned char)rnd(&seed);c->_ecx=(c->_ecx&0xffffff00)|(i&255);c->_eflags=(c->_eflags&0xffff0000)|(i&65535);}
static uint32_t result(uint32_t left,uint32_t source,unsigned count,uint32_t *flags){unsigned n=count&31;if(!n)return left;uint64_t joined=((uint64_t)left<<32)|source;uint32_t out=(uint32_t)((joined<<n)>>32);uint32_t bits=(left>>(32-n))&1;if(!out)bits|=64;if(out>>31)bits|=128;unsigned ones=0;for(unsigned i=0;i<8;++i)ones+=(out>>i)&1;if(!(ones&1))bits|=4;uint32_t mask=1|4|64|128;if(n==1){mask|=2048;if((left^out)>>31)bits|=2048;}*flags=(*flags&~mask)|bits;return out;}
static void expected(_cpu *c,_cpu *e,const move_case *v,unsigned char *mem,uint32_t *output){uint32_t left;unsigned n=v->cl?(c->_ecx&255):v->count;if(v->kind)memcpy(&left,mem,4);else left=((uint32_t *)c)[v->dest];*e=*c;*output=result(left,((uint32_t *)c)[v->source],n,&e->_eflags);if(!v->kind)((uint32_t *)e)[v->dest]=*output;}
int main(void){unsigned long total=0;unsigned guarded=0;unsigned char arena[48];for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r?256:65536);++i){const move_case *v=&moves[r];_cpu c,e;prepare(&c,r,i);if(!v->kind&&i<64){((uint32_t *)&c)[v->dest]=i<32?1u<<i:0;((uint32_t *)&c)[v->source]=i>=32?1u<<(i-32):0;if(v->cl)c._ecx=(c._ecx&0xffffff00)|(i&255);}unsigned char *mem=arena+8+i%8;c._pointer_offset=(uintptr_t)mem-address(&c,v);memset(arena,0xa6,sizeof arena);uint32_t seed=r+i+13,left=rnd(&seed);memcpy(mem,&left,4);uint32_t value;expected(&c,&e,v,mem,&value);unsigned char after[48];memcpy(after,arena,48);if(v->kind)memcpy(after+(mem-arena),&value,4);bodies[r](&c);assert(!memcmp(&c,&e,sizeof c));assert(!memcmp(arena,after,48));++total;}
size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *v=&moves[r];if(!v->kind)continue;for(unsigned i=0;i<256;++i){_cpu c,e;prepare(&c,r,i);unsigned char *mem=guard+page-4;c._pointer_offset=(uintptr_t)mem-address(&c,v);uint32_t seed=i*17+r,left=rnd(&seed),value;memcpy(mem,&left,4);expected(&c,&e,v,mem,&value);bodies[r](&c);uint32_t actual;memcpy(&actual,mem,4);assert(actual==value&&!memcmp(&c,&e,sizeof c));++total;++guarded;}}
assert(!munmap(guard,page*2));printf("PASS: %lu emitted SHLD full CPU checks; %u protected4byte RMW checks\n",total,guarded);return 0;}
