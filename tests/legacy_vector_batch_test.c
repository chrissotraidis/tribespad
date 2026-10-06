#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "moves.inc"
static uint32_t next(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static uint32_t addr(_cpu *c,const move_case *m){uint32_t g[8];memcpy(g,c,32);return m->disp+(m->base<0?0:g[m->base])+(m->index<0?0:g[m->index]*m->scale);}
static void prepare(_cpu *c,unsigned r,unsigned sample){uint32_t seed=0x9ed713+r*257+sample;for(unsigned i=0;i<sizeof *c;++i)((unsigned char *)c)[i]=(unsigned char)next(&seed);uint32_t g[8]={0x1000,0x1010,0x1020,0x1030,0x1040,0x1050,0x1060,0x1070};memcpy(c,g,32);}
static void expected(_cpu *c,_cpu *after,const move_case *m,unsigned char *mem){
 uint64_t a[2],b[2],result[2];memcpy(a,c->_xmm[m->dest],16);memcpy(b,m->kind==1?mem:c->_xmm[m->source],16);*after=*c;
 if(m->kind==2){memcpy(mem,b,16);return;}
 for(unsigned i=0;i<2;++i)switch(m->op){case 0:result[i]=a[i]&b[i];break;case 1:result[i]=a[i]|b[i];break;case 2:result[i]=a[i]^b[i];break;case 3:result[i]=~a[i]&b[i];break;case 4:result[i]=i?b[0]:a[0];break;case 5:result[i]=i?b[1]:a[1];break;case 6:result[i]=b[i];break;default:abort();}
 memcpy(after->_xmm[m->dest],result,16);
}
int main(void){unsigned long cases=0;unsigned guards=0,faults=0;unsigned char arena[64],wanted[64];size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(0,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED&&!mprotect(guard+page,page,PROT_NONE));
 for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *m=&moves[r];
  for(unsigned i=0;i<256;++i){_cpu c,after;prepare(&c,r,i);uint32_t a=addr(&c,m);if(m->kind&&m->base>=0){uint32_t g[8];memcpy(g,&c,32);g[m->base]-=a&15;memcpy(&c,g,32);a=addr(&c,m);}assert(!m->kind||!(a&15));unsigned char *mem=arena+16+(i%8);memset(arena,0xa6,sizeof arena);for(unsigned k=0;k<16;++k)mem[k]=(unsigned char)(i*17+k*31);if(i<128){memset(c._xmm[m->dest],0,16);c._xmm[m->dest][i/8]=(unsigned char)(1u<<(i%8));if(!m->kind){memset(c._xmm[m->source],0xff,16);c._xmm[m->source][(i/8+7)%16]^=(unsigned char)(1u<<(i%8));}}
   c._pointer_offset=(uintptr_t)mem-a;memcpy(wanted,arena,64);expected(&c,&after,m,wanted+(mem-arena));bodies[r](&c);assert(!memcmp(&c,&after,sizeof c)&&!memcmp(arena,wanted,64));++cases;
  }
  if(m->kind){_cpu c,after;prepare(&c,r,0);uint32_t a=addr(&c,m);if(m->base>=0){uint32_t g[8];memcpy(g,&c,32);g[m->base]-=a&15;memcpy(&c,g,32);a=addr(&c,m);}c._pointer_offset=(uintptr_t)(guard+page-16)-a;unsigned char bytes[16];memset(guard+page-16,0x5a,16);memcpy(bytes,guard+page-16,16);expected(&c,&after,m,bytes);bodies[r](&c);assert(!memcmp(&c,&after,sizeof c)&&!memcmp(guard+page-16,bytes,16));++guards;}
 }
 _cpu *shared=mmap(0,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);unsigned seen[7][3]={{0}};
 for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *m=&moves[r];if(!m->kind||m->base<0||m->base==m->index||seen[m->op][m->kind]++)continue;
  for(unsigned kind=0;kind<3;++kind){if(kind==2&&m->kind==2)continue;prepare(shared,r,0);memset(shared->_xmm[m->dest],0,16);uint32_t g[8];memcpy(g,shared,32);g[m->base]-=addr(shared,m)&15;if(kind==0)g[m->base]++;memcpy(shared,g,32);shared->_pointer_offset=(uintptr_t)(kind==0?arena:guard+page-(kind==2?8:0))-addr(shared,m);_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[r](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status));assert(kind==0?WTERMSIG(status)==SIGABRT:(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));++faults;}
 }
 assert(!munmap(shared,page)&&!munmap(guard,page*2));printf("PASS: %lu vector batch cases; %u protected16byte accesses; %u host faults; full CPU preservation\n",cases,guards,faults);return 0;}
