#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(TRIBESPAD_PMINSD_HOST_FAULT_TESTS)
#include <signal.h>
#include <sys/wait.h>
#endif
#include "moves.inc"
#include "pminsd_vectors.h"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static void prepare(_cpu *c,unsigned r,unsigned i){uint32_t seed=0xa0d+r*13+i;for(unsigned k=0;k<sizeof *c;++k)((unsigned char *)c)[k]=(unsigned char)rnd(&seed);c->_mxcsr=i&65535;c->_eax=0x1000;c->_esp=0x1100;c->_ebx=0x1400;c->_esi=4;}
static uint32_t address(_cpu *c,const move_case *v){return v->address==0?c->_eax:v->address==1?c->_esp+16:v->address==2?c->_ebx+c->_esi*4-16:v->address==3?0x200010:0x101cb1d0;}
static void apply(_cpu *c,_cpu *expected,const move_case *v,unsigned i,unsigned char *mem,unsigned exhaustive){unsigned char a[16],b[16];uint32_t left[4],right_values[4];if(exhaustive)pminsd_pattern(i,left,right_values);else for(unsigned k=0;k<4;++k){if(i<256){left[k]=i<128&&k==i/32?(1u<<(i%32)):0;right_values[k]=i>=128&&k==(i-128)/32?(1u<<((i-128)%32)):0;}else if(i<384){static const uint32_t edges[]={0,1,0x7fffffff,0x80000000,0xfffffffe,0xffffffff,0xffff,0x10000};left[k]=edges[(i/8+k)%8];right_values[k]=edges[(i+k)%8];}else{uint32_t seed=i*613+k*8191;left[k]=rnd(&seed);right_values[k]=rnd(&seed);}}memcpy(a,left,16);memcpy(b,right_values,16);memcpy(c->_xmm[v->dest],a,16);if(v->kind)memcpy(mem,b,16);else memcpy(c->_xmm[v->source],b,16);*expected=*c;const unsigned char *right=v->kind?mem:c->_xmm[v->source];for(unsigned k=0;k<4;++k){uint32_t l,r;memcpy(&l,c->_xmm[v->dest]+k*4,4);memcpy(&r,right+k*4,4);int64_t a64=(int64_t)l-((l>>31)?INT64_C(4294967296):0),b64=(int64_t)r-((r>>31)?INT64_C(4294967296):0);uint32_t value=a64<b64?l:r;memcpy(expected->_xmm[v->dest]+k*4,&value,4);}}

int main(void){unsigned char arena[96];unsigned long total=0;unsigned protected_count=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r<1?65536:512);++i){const move_case *v=&moves[r];_cpu c,expected;prepare(&c,r,i);unsigned char *mem=arena+16+i%16;c._pointer_offset=(uintptr_t)mem-address(&c,v);memset(arena,0xa6,sizeof arena);apply(&c,&expected,v,i,mem,r<1);unsigned char saved[96];memcpy(saved,arena,96);bodies[r](&c);assert(!memcmp(&c,&expected,sizeof c));assert(!memcmp(saved,arena,96));++total;}
size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *v=&moves[r];if(!v->kind)continue;for(unsigned i=0;i<512;++i){_cpu c,expected;prepare(&c,r,i);c._pointer_offset=(uintptr_t)(guard+page-16)-address(&c,v);apply(&c,&expected,v,i,guard+page-16,0);bodies[r](&c);assert(!memcmp(&c,&expected,sizeof c));++total;++protected_count;}}
unsigned faults=0;
#if defined(TRIBESPAD_PMINSD_HOST_FAULT_TESTS)
_cpu *shared=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);
for(unsigned misalign=1;misalign<16;++misalign){prepare(shared,9,0);shared->_eax+=misalign;shared->_pointer_offset=(uintptr_t)arena-shared->_eax;_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[9](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(shared,&before,sizeof before));++faults;}
for(unsigned accessible=0;accessible<=4;accessible+=4){
prepare(shared,9,0);memset(shared->_xmm[0],255,16);shared->_pointer_offset=(uintptr_t)(guard+page-accessible)-shared->_eax;_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[9](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));++faults;
}assert(!munmap(shared,page));
#endif
assert(!munmap(guard,page*2));printf("PASS: %lu emitted legacy PMINSD checks; %u protected16byte reads; %u host faults\n",total,protected_count,faults);return 0;}
