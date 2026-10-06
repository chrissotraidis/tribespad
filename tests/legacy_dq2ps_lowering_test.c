#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(TRIBESPAD_CVTDQ2PS_HOST_FAULT_TESTS)
#include <signal.h>
#include <sys/wait.h>
#endif
#include "softfloat.h"
#include "moves.inc"
#include "cases.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static void prepare(_cpu *c,unsigned r,unsigned i){uint32_t seed=0xa0d+r*13+i;for(unsigned k=0;k<sizeof *c;++k)((unsigned char *)c)[k]=(unsigned char)rnd(&seed);c->_mxcsr=i&65535;c->_eax=0x1000;c->_esp=0x1100;c->_ebx=0x1400;c->_esi=4;}
static uint32_t address(_cpu *c,const move_case *v){return v->address==0?c->_eax:v->address==1?c->_esp+16:v->address==2?c->_ebx+c->_esi*4-16:v->address==3?0x200010:0x101cb190;}
static void apply(_cpu *c,_cpu *expected,const move_case *v,const sample *q,unsigned char *mem){c->_mxcsr=q->control;if(v->kind)memcpy(mem,q->input,16);else memcpy(c->_xmm[v->source],q->input,16);*expected=*c;expected->_mxcsr=q->after;memcpy(expected->_xmm[v->dest],q->value,16);}
static void execute(unsigned r,_cpu *c){softfloat_roundingMode=3;softfloat_detectTininess=0;softfloat_exceptionFlags=27;extF80_roundingPrecision=32;bodies[r](c);assert(softfloat_roundingMode==3&&softfloat_detectTininess==0&&softfloat_exceptionFlags==27&&extF80_roundingPrecision==32);}
int main(void){unsigned char arena[96];unsigned long total=0;unsigned protected_count=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r?128:sizeof corpus/sizeof corpus[0]);++i){const move_case *v=&moves[r];_cpu c,expected;prepare(&c,r,i);unsigned char *mem=arena+16+i%16;c._pointer_offset=(uintptr_t)mem-address(&c,v);memset(arena,0xa6,sizeof arena);apply(&c,&expected,v,r?&samples[i]:&corpus[i],mem);unsigned char saved[96];memcpy(saved,arena,96);execute(r,&c);assert(!memcmp(&c,&expected,sizeof c));assert(!memcmp(saved,arena,96));++total;}
size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *v=&moves[r];if(!v->kind)continue;for(unsigned i=0;i<128;++i){_cpu c,expected;prepare(&c,r,i);c._pointer_offset=(uintptr_t)(guard+page-16)-address(&c,v);apply(&c,&expected,v,&samples[i],guard+page-16);execute(r,&c);assert(!memcmp(&c,&expected,sizeof c));++total;++protected_count;}}
unsigned faults=0;
#if defined(TRIBESPAD_CVTDQ2PS_HOST_FAULT_TESTS)
_cpu *shared=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);
for(unsigned misalign=1;misalign<16;++misalign){prepare(shared,9,0);shared->_eax+=misalign;shared->_pointer_offset=(uintptr_t)arena-shared->_eax;_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[9](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(shared,&before,sizeof before));++faults;}
for(unsigned partial=0;partial<2;++partial){prepare(shared,9,0);shared->_mxcsr=0x0f80;memset(guard+page-4,0,4);shared->_pointer_offset=(uintptr_t)(guard+page-(partial?4:0))-shared->_eax;_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[9](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));++faults;}
for(unsigned lane=0;lane<4;++lane){prepare(shared,0,0);shared->_mxcsr=0x1f80&~(32u<<7);uint32_t input[4]={0,1,2,3};input[lane]=0x01000001;memcpy(shared->_xmm[2],input,16);_cpu expected=*shared;expected._mxcsr|=32;pid_t probe=fork();assert(probe>=0);if(!probe){bodies[0](shared);_exit(7);}int result;assert(waitpid(probe,&result,0)==probe&&WIFSIGNALED(result)&&WTERMSIG(result)==SIGABRT);assert(!memcmp(shared,&expected,sizeof expected));++faults;}
prepare(shared,0,0);shared->_mxcsr=0x0f80;uint32_t exact[4]={0,1,0x80000000,0x01000000},value[4]={0,0x3f800000,0xcf000000,0x4b800000};memcpy(shared->_xmm[2],exact,16);_cpu expected=*shared;memcpy(expected._xmm[2],value,16);execute(0,shared);assert(!memcmp(shared,&expected,sizeof expected));
assert(!munmap(shared,page));
#endif
assert(!munmap(guard,page*2));printf("PASS: %lu emitted legacy CVTDQ2PS checks; %u protected16byte reads; %u host faults\n",total,protected_count,faults);return 0;}
