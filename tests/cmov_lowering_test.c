#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(TRIBESPAD_CMOV_HOST_FAULT_TESTS)
#include <signal.h>
#include <sys/wait.h>
#endif
#include "moves.inc"
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static unsigned condition(unsigned cc,uint32_t f){unsigned c=!!(f&1),p=!!(f&4),z=!!(f&64),s=!!(f&128),o=!!(f&2048);unsigned table[16]={o,!o,c,!c,z,!z,c||z,!(c||z),s,!s,p,!p,s!=o,s==o,z||(s!=o),!z&&(s==o)};return table[cc];}
static uint32_t flags(unsigned i){return ((i&1)?1:0)|((i&2)?4:0)|((i&4)?64:0)|((i&8)?128:0)|((i&16)?2048:0);}
static void prepare(_cpu *c,unsigned r,unsigned i){uint32_t seed=0xc001+r*127+i;for(unsigned k=0;k<sizeof *c;++k)((unsigned char *)c)[k]=(unsigned char)rnd(&seed);c->_eflags=(c->_eflags&~0x8c5)|flags(i);c->_eax=0x1003;c->_esp=0x1100;c->_ebx=0x1400;c->_esi=0x11;if(moves[r].compare){static const uint32_t edge[]={0,1,0xffffffff,0x80000000,0x7fffffff,0x10000,0xffff,0x12345678};c->_eax=edge[i%8];c->_ecx=edge[i/8%8];}}
static uint32_t address(_cpu *c,const move_case *v){return v->address==0?c->_eax:v->address==1?c->_esp+16:v->address==2?c->_ebx+c->_esi*4-17:0x200003;}
static void oracle(_cpu *c,const unsigned char *mem,const move_case *v){uint32_t words[8];memcpy(words,c,32);if(v->compare){uint32_t a=c->_eax,b=c->_ecx,d=a-b;unsigned parity=0;for(unsigned k=0;k<8;++k)parity^=(d>>k)&1;c->_eflags=(c->_eflags&~0x8d5)|((a<b)?1:0)|(!parity?4:0)|(((a^b^d)&16)?16:0)|(!d?64:0)|((d>>31)?128:0)|((((a^b)&(a^d))>>31)?2048:0);}uint32_t value=words[v->source];if(v->kind){value=0;for(unsigned k=0;k<v->bits/8;++k)value|=(uint32_t)mem[k]<<(8*k);}if(condition(v->cc,c->_eflags)){if(v->bits==16)value=(words[v->dest]&0xffff0000)|(value&65535);memcpy((unsigned char *)c+v->dest*4,&value,4);}}
int main(void){unsigned char *arena=malloc(0x220100);assert(arena);unsigned long total=0;unsigned protected_count=0;
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r)for(unsigned i=0;i<64;++i){const move_case *v=&moves[r];_cpu c;prepare(&c,r,i);c._pointer_offset=(uintptr_t)(arena+i%16);unsigned char *base=(void *)(uintptr_t)c._pointer_offset;uint32_t offset=address(&c,v);unsigned char saved[80];if(v->kind){memset(base+offset-16,0xa6,80);uint32_t value=0x1234beefu+i;memcpy(base+offset,&value,4);memcpy(saved,base+offset-16,80);} _cpu expected=c;oracle(&expected,v->kind?base+offset:NULL,v);bodies[r](&c);if(memcmp(&c,&expected,sizeof c)){fprintf(stderr,"body %u sample %u mismatch\n",r,i);abort();}if(v->kind)assert(!memcmp(base+offset-16,saved,80));++total;}
size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));
for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *v=&moves[r];if(!v->kind)continue;for(unsigned i=0;i<32;++i){_cpu c;prepare(&c,r,i);uint32_t offset=address(&c,v);unsigned n=v->bits/8;c._pointer_offset=(uintptr_t)(guard+page-n)-offset;memset(guard,0xa6,page);_cpu expected=c;oracle(&expected,guard+page-n,v);bodies[r](&c);assert(!memcmp(&c,&expected,sizeof c));++total;++protected_count;}}
unsigned faults=0;
#if defined(TRIBESPAD_CMOV_HOST_FAULT_TESTS)
_cpu *shared=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);
for(unsigned bits=16;bits<=32;bits+=16)for(unsigned cc=0;cc<16;++cc){unsigned r=0;while(r<sizeof moves/sizeof moves[0]&&!(moves[r].kind&&moves[r].bits==bits&&moves[r].cc==cc&&moves[r].address==0))++r;assert(r<sizeof moves/sizeof moves[0]);unsigned i=0;while(condition(cc,flags(i)))++i;prepare(shared,r,i);shared->_pointer_offset=(uintptr_t)(guard+page)-address(shared,&moves[r]);_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[r](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));++faults;}assert(!munmap(shared,page));
#endif
assert(!munmap(guard,page*2));free(arena);printf("PASS: %lu emitted conditional move checks; %u protected reads; %u false-condition faults\n",total,protected_count,faults);return 0;}
