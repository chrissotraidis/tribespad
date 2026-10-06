#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(TRIBESPAD_SHUFPS_HOST_FAULT_TESTS)
#include <signal.h>
#include <sys/wait.h>
#endif
#include "moves.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static void prepare(_cpu *c,unsigned r,unsigned i){uint32_t seed=0xa0d+r*13+i;for(unsigned k=0;k<sizeof *c;++k)((unsigned char *)c)[k]=(unsigned char)rnd(&seed);c->_mxcsr=i&65535;c->_eax=0x1000;c->_esp=0x1100;c->_ebx=0x1400;c->_esi=4;}
static uint32_t address(_cpu *c,const move_case *v){return v->address==0?c->_eax:v->address==1?c->_esp+16:v->address==2?c->_ebx+c->_esi*4-16:v->address==3?0x200010:0x101cb190;}
static void apply(_cpu *c,_cpu *expected,const move_case *v,unsigned i,unsigned char *mem,unsigned exhaustive){uint32_t a[4],b[4];for(unsigned k=0;k<4;++k){if(exhaustive&&i<128){a[k]=k==i/32?1u<<(i%32):0;b[k]=~a[k];}else{a[k]=i==0?0:i==1?0xffffffffu:i==2?0x7fa12345u:i==3?0xff800000u:0x10203040u+k*0x11111111u+i*0x12345u;b[k]=~a[k]+k;}}memcpy(c->_xmm[v->dest],a,16);if(v->kind)memcpy(mem,b,16);else memcpy(c->_xmm[v->source],b,16);*expected=*c;uint32_t left[4],right[4],value[4];memcpy(left,c->_xmm[v->dest],16);memcpy(right,v->kind?mem:c->_xmm[v->source],16);for(unsigned k=0;k<4;++k)value[k]=(k<2?left:right)[(v->control>>(k*2))&3];memcpy(expected->_xmm[v->dest],value,16);}
int main(void){unsigned char arena[96];unsigned long total=0;unsigned protected_count=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<(r?8:65536);++i){const move_case *v=&moves[r];_cpu c,expected;prepare(&c,r,i);unsigned char *mem=arena+16+i%16;c._pointer_offset=(uintptr_t)mem-address(&c,v);memset(arena,0xa6,sizeof arena);apply(&c,&expected,v,i,mem,!r);unsigned char saved[96];memcpy(saved,arena,96);bodies[r](&c);assert(!memcmp(&c,&expected,sizeof c));assert(!memcmp(saved,arena,96));++total;}
size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *v=&moves[r];if(!v->kind)continue;for(unsigned i=0;i<2;++i){_cpu c,expected;prepare(&c,r,i);c._pointer_offset=(uintptr_t)(guard+page-16)-address(&c,v);apply(&c,&expected,v,i,guard+page-16,0);bodies[r](&c);assert(!memcmp(&c,&expected,sizeof c));++total;++protected_count;}}
unsigned faults=0;
#if defined(TRIBESPAD_SHUFPS_HOST_FAULT_TESTS)
_cpu *shared=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);
for(unsigned misalign=1;misalign<16;++misalign){prepare(shared,9,0);shared->_eax+=misalign;shared->_pointer_offset=(uintptr_t)arena-shared->_eax;_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[9](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(shared,&before,sizeof before));++faults;}
for(unsigned control=0;control<256;++control){unsigned body=9+control*96;prepare(shared,body,0);shared->_pointer_offset=(uintptr_t)(guard+page-4)-shared->_eax;_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[body](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));++faults;}assert(!munmap(shared,page));
#endif
assert(!munmap(guard,page*2));printf("PASS: %lu emitted legacy SHUFPS checks; %u protected16byte reads; %u host faults\n",total,protected_count,faults);return 0;}
