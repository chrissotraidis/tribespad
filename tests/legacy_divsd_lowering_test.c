#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(TRIBESPAD_DIVSD_HOST_FAULT_TESTS)
#include <signal.h>
#include <sys/wait.h>
#endif
#ifndef LITTLEENDIAN
#define LITTLEENDIAN 1
#endif
#include "softfloat.h"
#include "moves.inc"
#include "cases.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static void prepare(_cpu *c,unsigned r,unsigned i){uint32_t seed=0xd1f+r*13+i;for(unsigned k=0;k<sizeof *c;++k)((unsigned char *)c)[k]=(unsigned char)rnd(&seed);c->_eax=0x1003;c->_esp=0x1100;c->_ebx=0x1400;c->_esi=0x11;}
static uint32_t address(_cpu *c,const move_case *v){return v->address==0?c->_eax:v->address==1?c->_esp+16:v->address==2?c->_ebx+c->_esi*4-17:v->address==3?0x200003:0x101cae08;}
static void apply(_cpu *c,_cpu *expected,const move_case *v,const sample *q,unsigned char *memory){c->_mxcsr=q->control;memcpy(c->_xmm[v->dest],&q->a,8);if(v->kind)memcpy(memory,&q->b,8);else memcpy(c->_xmm[v->source],&q->b,8);*expected=*c;expected->_mxcsr=q->after;memcpy(expected->_xmm[v->dest],&q->value,8);}
int main(void){unsigned char arena[80];unsigned long total=0;unsigned protected_count=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r?128:sizeof corpus/sizeof corpus[0]);++i){const move_case *v=&moves[r];sample q;if(!r)q=corpus[i];else {const short_sample *s=&samples[i];q=(sample){s->control,s->a,s->b,s->value,s->after};if(!v->kind&&v->dest==v->source){q.b=q.a;q.value=s->same;q.after=s->same_after;}}_cpu c,expected;prepare(&c,r,i);uint32_t offset=address(&c,v);unsigned char *memory=arena+16+i%16;c._pointer_offset=(uintptr_t)memory-offset;memset(arena,0xa6,sizeof arena);apply(&c,&expected,v,&q,memory);unsigned char saved[80];memcpy(saved,arena,80);softfloat_roundingMode=softfloat_round_max;softfloat_detectTininess=softfloat_tininess_beforeRounding;softfloat_exceptionFlags=0x55;extF80_roundingPrecision=32;bodies[r](&c);assert(softfloat_roundingMode==softfloat_round_max&&softfloat_detectTininess==softfloat_tininess_beforeRounding&&softfloat_exceptionFlags==0x55&&extF80_roundingPrecision==32);if(memcmp(&c,&expected,sizeof c)){fprintf(stderr,"DIVSD body %u sample %u mismatch\n",r,i);abort();}assert(!memcmp(saved,arena,80));++total;}
size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *v=&moves[r];if(!v->kind)continue;for(unsigned i=0;i<128;++i){const short_sample *s=&samples[i];sample q={s->control,s->a,s->b,s->value,s->after};_cpu c,expected;prepare(&c,r,i);c._pointer_offset=(uintptr_t)(guard+page-8)-address(&c,v);apply(&c,&expected,v,&q,guard+page-8);softfloat_roundingMode=softfloat_round_max;softfloat_detectTininess=softfloat_tininess_beforeRounding;softfloat_exceptionFlags=0x55;extF80_roundingPrecision=32;bodies[r](&c);assert(softfloat_roundingMode==softfloat_round_max&&softfloat_detectTininess==softfloat_tininess_beforeRounding&&softfloat_exceptionFlags==0x55&&extF80_roundingPrecision==32);assert(!memcmp(&c,&expected,sizeof c));++total;++protected_count;}}
unsigned faults=0;
#if defined(TRIBESPAD_DIVSD_HOST_FAULT_TESTS)
_cpu *shared=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);
const uint32_t unmask[]={1,2,4,32,8,16},raised[]={1,2,4,32,40,48};const uint64_t left[]={UINT64_C(0x7ff0123456789abc),1,UINT64_C(0x3ff0000000000000),UINT64_C(0x3ff0000000000000),UINT64_C(0x7fefffffffffffff),UINT64_C(0x0010000000000001)},right[]={UINT64_C(0x3ff0000000000000),UINT64_C(0x3ff0000000000000),0,UINT64_C(0x4008000000000000),UINT64_C(0x3fe0000000000000),UINT64_C(0x4000000000000000)};
for(unsigned which=0;which<6;++which){prepare(shared,0,0);shared->_mxcsr=0x1f80&~(unmask[which]<<7);uint64_t a=left[which],b=right[which];memcpy(shared->_xmm[0],&a,8);memcpy(shared->_xmm[2],&b,8);_cpu expected=*shared;expected._mxcsr|=raised[which];pid_t child=fork();assert(child>=0);if(!child){bodies[0](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);if(memcmp(shared,&expected,sizeof expected)){fprintf(stderr,"DIVSD stop %u MXCSR %x expected %x\n",which,shared->_mxcsr,expected._mxcsr);abort();}++faults;}
/* A masked NaN must not suppress an inaccessible MEM source read. */
prepare(shared,9,0);shared->_mxcsr=0x1f80;uint64_t nan=UINT64_C(0x7ff8123456789abc);memcpy(shared->_xmm[0],&nan,8);shared->_pointer_offset=(uintptr_t)(guard+page)-address(shared,&moves[9]);_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[9](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));assert(!munmap(shared,page));
#endif
assert(!munmap(guard,page*2));printf("PASS: %lu emitted legacy DIVSD checks; %u protected8byte reads; %u unmasked stops; TLS/full CPU preservation\n",total,protected_count,faults);return 0;}
