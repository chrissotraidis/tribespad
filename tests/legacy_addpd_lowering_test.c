#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(TRIBESPAD_ADDPD_HOST_FAULT_TESTS)
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
static void prepare(_cpu *c,unsigned r,unsigned i){uint32_t seed=0xd1f+r*13+i;for(unsigned k=0;k<sizeof *c;++k)((unsigned char *)c)[k]=(unsigned char)rnd(&seed);c->_eax=0x1000;c->_esp=0x1100;c->_ebx=0x1400;c->_esi=4;}
static uint32_t address(_cpu *c,const move_case *v){return v->address==0?c->_eax:v->address==1?c->_esp+16:v->address==2?c->_ebx+c->_esi*4-16:0x200010;}
static void apply(_cpu *c,_cpu *expected,const move_case *v,const sample *q,unsigned char *mem){uint64_t left[]={q->a,q->c},right[]={q->b,q->d},value[]={q->v0,q->v1};c->_mxcsr=q->control;memcpy(c->_xmm[v->dest],left,16);if(v->kind)memcpy(mem,right,16);else memcpy(c->_xmm[v->source],right,16);*expected=*c;expected->_mxcsr=q->after;memcpy(expected->_xmm[v->dest],value,16);}
static void run(unsigned r,_cpu *c){softfloat_roundingMode=softfloat_round_max;softfloat_detectTininess=softfloat_tininess_beforeRounding;softfloat_exceptionFlags=0x55;extF80_roundingPrecision=32;bodies[r](c);assert(softfloat_roundingMode==softfloat_round_max&&softfloat_detectTininess==softfloat_tininess_beforeRounding&&softfloat_exceptionFlags==0x55&&extF80_roundingPrecision==32);}
int main(void){unsigned char arena[80];unsigned long total=0;unsigned protected_count=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r?128:sizeof corpus/sizeof corpus[0]);++i){const move_case *v=&moves[r];sample q;if(!r)q=corpus[i];else{const short_sample *s=&samples[i];q=s->q;if(!v->kind&&v->dest==v->source){q.b=q.a;q.d=q.c;q.v0=s->same0;q.v1=s->same1;q.after=s->same_after;}}_cpu c,expected;prepare(&c,r,i);unsigned char *mem=arena+16+i%16;c._pointer_offset=(uintptr_t)mem-address(&c,v);memset(arena,0xa6,sizeof arena);apply(&c,&expected,v,&q,mem);unsigned char saved[80];memcpy(saved,arena,80);run(r,&c);if(memcmp(&c,&expected,sizeof c)){fprintf(stderr,"ADDPD body %u sample %u mismatch\n",r,i);abort();}assert(!memcmp(saved,arena,80));++total;}
size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *v=&moves[r];if(!v->kind)continue;for(unsigned i=0;i<128;++i){sample q=samples[i].q;_cpu c,expected;prepare(&c,r,i);c._pointer_offset=(uintptr_t)(guard+page-16)-address(&c,v);apply(&c,&expected,v,&q,guard+page-16);run(r,&c);assert(!memcmp(&c,&expected,sizeof c));++total;++protected_count;}}
unsigned faults=0;
#if defined(TRIBESPAD_ADDPD_HOST_FAULT_TESTS)
_cpu *shared=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);
const uint32_t unmask[]={1,2,32,8,16},raised[]={1,2,32,40,50};
const uint64_t left[]={UINT64_C(0x7ff0123456789abc),1,UINT64_C(0x3ff0000000000000),UINT64_C(0x7fefffffffffffff),1},right[]={UINT64_C(0x3ff0000000000000),0,UINT64_C(0x3ca0000000000000),UINT64_C(0x7fefffffffffffff),0};
for(unsigned lane=0;lane<2;++lane)for(unsigned which=0;which<5;++which){prepare(shared,0,0);shared->_mxcsr=(0x1f80|(which==4?0x8000:0))&~(unmask[which]<<7);uint64_t a[2]={UINT64_C(0x4000000000000000),UINT64_C(0x4000000000000000)},b[2]={UINT64_C(0x3ff0000000000000),UINT64_C(0x3ff0000000000000)};a[lane]=left[which];b[lane]=right[which];memcpy(shared->_xmm[1],a,16);memcpy(shared->_xmm[0],b,16);_cpu expected=*shared;expected._mxcsr|=raised[which];pid_t child=fork();assert(child>=0);if(!child){bodies[0](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);if(memcmp(shared,&expected,sizeof expected)){fprintf(stderr,"ADDPD stop %u lane%u MXCSR %x expected %x\n",which,lane,shared->_mxcsr,expected._mxcsr);abort();}++faults;}
for(unsigned offset=1;offset<16;++offset){prepare(shared,9,0);shared->_eax+=offset;shared->_pointer_offset=(uintptr_t)arena-shared->_eax;_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[9](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(shared,&before,sizeof before));++faults;}
for(unsigned accessible=0;accessible<=8;accessible+=8){prepare(shared,9,0);shared->_mxcsr=0x1f80;uint64_t nan=UINT64_C(0x7ff8123456789abc);memcpy(shared->_xmm[0],&nan,8);shared->_pointer_offset=(uintptr_t)(guard+page-accessible)-address(shared,&moves[9]);_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[9](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));++faults;}assert(!munmap(shared,page));
#endif
assert(!munmap(guard,page*2));printf("PASS: %lu emitted legacy ADDPD checks; %u protected16byte reads; %u host faults; TLS/full CPU preservation\n",total,protected_count,faults);return 0;}
