#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "moves.inc"
#include "cases.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static void prepare(_cpu *c,unsigned r,unsigned i){uint32_t seed=0xd1f+r*13+i;for(unsigned k=0;k<sizeof *c;++k)((unsigned char *)c)[k]=(unsigned char)rnd(&seed);c->_eax=0x1003;c->_esp=0x1100;c->_ebx=0x1400;c->_esi=0x11;}
static uint32_t address(_cpu *c,const move_case *v){return v->address==0?c->_eax:v->address==1?c->_esp+16:v->address==2?c->_ebx+c->_esi*4-17:v->address==3?0x200003:0x101caf34;}
static void apply(_cpu *c,_cpu *expected,const move_case *v,const sample *q,unsigned char *memory){c->_mxcsr=q->control;memcpy(c->_xmm[v->dest],&q->a,4);if(v->kind)memcpy(memory,&q->b,4);else memcpy(c->_xmm[v->source],&q->b,4);*expected=*c;expected->_mxcsr=q->after;memcpy(expected->_xmm[v->dest],&q->value,4);}
int main(void){unsigned char arena[80];unsigned long total=0;unsigned protected_count=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r?128:sizeof corpus/sizeof corpus[0]);++i){const move_case *v=&moves[r];sample q;if(!r)q=corpus[i];else {const short_sample *s=&samples[i];q=(sample){s->control,s->a,s->b,s->value,s->after};if(!v->kind&&v->dest==v->source){q.b=q.a;q.value=s->same;q.after=s->same_after;}}_cpu c,expected;prepare(&c,r,i);uint32_t offset=address(&c,v);unsigned char *memory=arena+16+i%16;c._pointer_offset=(uintptr_t)memory-offset;memset(arena,0xa6,sizeof arena);apply(&c,&expected,v,&q,memory);unsigned char saved[80];memcpy(saved,arena,80);bodies[r](&c);if(memcmp(&c,&expected,sizeof c)){fprintf(stderr,"DIVSS body %u sample %u mismatch\n",r,i);abort();}assert(!memcmp(saved,arena,80));++total;}
size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *v=&moves[r];if(!v->kind)continue;for(unsigned i=0;i<128;++i){const short_sample *s=&samples[i];sample q={s->control,s->a,s->b,s->value,s->after};_cpu c,expected;prepare(&c,r,i);c._pointer_offset=(uintptr_t)(guard+page-4)-address(&c,v);apply(&c,&expected,v,&q,guard+page-4);bodies[r](&c);assert(!memcmp(&c,&expected,sizeof c));++total;++protected_count;}}
assert(!munmap(guard,page*2));printf("PASS: %lu emitted legacy DIVSS checks; %u protected4byte reads; full CPU and legacy vector preservation\n",total,protected_count);return 0;}
