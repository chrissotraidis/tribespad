#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(TRIBESPAD_UCOMISS_HOST_FAULT_TESTS)
#include <signal.h>
#include <sys/wait.h>
#endif
#include "moves.inc"
#include "cases.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static void prepare(_cpu *c,unsigned r,unsigned i){uint32_t seed=0xd1f+r*13+i;for(unsigned k=0;k<sizeof *c;++k)((unsigned char *)c)[k]=(unsigned char)rnd(&seed);c->_eax=0x1003;c->_esp=0x1100;c->_ebx=0x1400;c->_esi=0x11;}
static uint32_t address(_cpu *c,const move_case *v){return v->address==0?c->_eax:v->address==1?c->_esp+16:v->address==2?c->_ebx+c->_esi*4-17:v->address==3?0x200003:0x101cad68;}
static void apply(_cpu *c,_cpu *expected,const move_case *v,const sample *q,unsigned char *memory){c->_mxcsr=q->control;c->_eflags=(c->_eflags&~0x8d5)|q->flags;memcpy(c->_xmm[v->dest],&q->a,4);if(v->kind)memcpy(memory,&q->b,4);else memcpy(c->_xmm[v->source],&q->b,4);*expected=*c;expected->_mxcsr=q->after;expected->_eflags=(c->_eflags&~0x8d5)|q->after_flags;}
int main(void){unsigned char arena[80];unsigned long total=0;unsigned protected_count=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r?128:sizeof corpus/sizeof corpus[0]);++i){const move_case *v=&moves[r];sample q;if(!r)q=corpus[i];else {const short_sample *s=&samples[i];q=(sample){s->control,s->a,s->b,s->flags,s->after,s->after_flags};if(!v->kind&&v->dest==v->source){q.b=q.a;q.after_flags=s->same_flags;q.after=s->same_after;}}_cpu c,expected;prepare(&c,r,i);uint32_t offset=address(&c,v);unsigned char *memory=arena+16+i%16;c._pointer_offset=(uintptr_t)memory-offset;memset(arena,0xa6,sizeof arena);apply(&c,&expected,v,&q,memory);unsigned char saved[80];memcpy(saved,arena,80);bodies[r](&c);if(memcmp(&c,&expected,sizeof c)){fprintf(stderr,"UCOMISS body %u sample %u mismatch\n",r,i);abort();}assert(!memcmp(saved,arena,80));++total;}
size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *v=&moves[r];if(!v->kind)continue;for(unsigned i=0;i<128;++i){const short_sample *s=&samples[i];sample q={s->control,s->a,s->b,s->flags,s->after,s->after_flags};_cpu c,expected;prepare(&c,r,i);c._pointer_offset=(uintptr_t)(guard+page-4)-address(&c,v);apply(&c,&expected,v,&q,guard+page-4);bodies[r](&c);assert(!memcmp(&c,&expected,sizeof c));++total;++protected_count;}}
unsigned faults=0;
#if defined(TRIBESPAD_UCOMISS_HOST_FAULT_TESTS)
_cpu *shared=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);
for(unsigned exception=1;exception<=2;++exception){prepare(shared,0,0);shared->_mxcsr=0x1f80&~(exception<<7);uint32_t a=exception==1?0x7f812345:1,b=0x3f800000;memcpy(shared->_xmm[0],&a,4);memcpy(shared->_xmm[3],&b,4);_cpu expected=*shared;expected._mxcsr|=exception;pid_t child=fork();assert(child>=0);if(!child){bodies[0](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(shared,&expected,sizeof expected));++faults;}
/* A masked NaN must not suppress an inaccessible MEM source read. */
prepare(shared,9,0);shared->_mxcsr=0x1f80;uint32_t nan=0x7fc12345;memcpy(shared->_xmm[0],&nan,4);shared->_pointer_offset=(uintptr_t)(guard+page)-address(shared,&moves[9]);_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[9](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));++faults;assert(!munmap(shared,page));
#endif
_cpu quiet,e;prepare(&quiet,0,0);quiet._mxcsr=0x1f00;uint32_t qnan=0x7fc12345,subnormal=1;memcpy(quiet._xmm[0],&qnan,4);memcpy(quiet._xmm[3],&subnormal,4);e=quiet;e._eflags=(e._eflags&~0x8d5)|0x45;bodies[0](&quiet);assert(!memcmp(&quiet,&e,sizeof quiet));
assert(!munmap(guard,page*2));printf("PASS: %lu emitted legacy UCOMISS checks; %u protected4byte reads; %u host faults; quietNaN unmasked success\n",total,protected_count,faults);return 0;}
