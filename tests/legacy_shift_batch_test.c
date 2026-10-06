#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t next(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
static const uint64_t counts[]={0,1,7,8,15,16,17,31,32,33,63,64,65,127,128,255,256,UINT32_MAX,UINT64_C(0x100000000),UINT64_C(0x8000000000000000),UINT64_MAX};
/* Independent per-bit routing model: no native signed shifts or lane arithmetic. */
static void expected_bits(unsigned op,const unsigned char in[16],uint64_t count,unsigned char out[16]) {
 memset(out,0,16);unsigned width=op==8?128:op==0||op==3||op==6?16:op==1||op==4||op==7?32:64;
 if(op==8){if(count>=16)return;count*=8;}
 for(unsigned lane=0;lane<128;lane+=width)for(unsigned bit=0;bit<width;++bit){
  int source=-1;
  if(op<3||op==8){if(count<=bit)source=(int)(bit-count);}
  else if(count<width-bit)source=(int)(bit+count);
  else if(op>=6)source=(int)width-1;
  if(source>=0){unsigned k=lane+(unsigned)source;if(in[k/8]&(1u<<(k%8)))out[(lane+bit)/8]|=(unsigned char)(1u<<((lane+bit)%8));}
 }
}
#ifdef REFERENCE_X86
#include <emmintrin.h>
static __m128i byteleft(__m128i a,unsigned n){switch(n){
#define CASE(N) case N:return _mm_slli_si128(a,N);
 CASE(0) CASE(1) CASE(2) CASE(3) CASE(4) CASE(5) CASE(6) CASE(7)
 CASE(8) CASE(9) CASE(10) CASE(11) CASE(12) CASE(13) CASE(14) CASE(15)
#undef CASE
 default:return _mm_setzero_si128();}}
int main(void){unsigned total=0;uint32_t seed=0x190712;
 for(unsigned op=0;op<9;++op)for(unsigned sample=0;sample<512;++sample){unsigned char in[16],result[16],want[16];uint64_t count=sample<sizeof counts/sizeof counts[0]?counts[sample]:sample<277?sample-21:((uint64_t)next(&seed)<<32)|next(&seed);if(op==8)count=sample%256;
  for(unsigned i=0;i<16;++i)in[i]=(unsigned char)next(&seed);
  __m128i a=_mm_loadu_si128((const __m128i *)in),b=_mm_set_epi64x((long long)UINT64_MAX,(long long)count),v;
  switch(op){case 0:v=_mm_sll_epi16(a,b);break;case 1:v=_mm_sll_epi32(a,b);break;case 2:v=_mm_sll_epi64(a,b);break;case 3:v=_mm_srl_epi16(a,b);break;case 4:v=_mm_srl_epi32(a,b);break;case 5:v=_mm_srl_epi64(a,b);break;case 6:v=_mm_sra_epi16(a,b);break;case 7:v=_mm_sra_epi32(a,b);break;case 8:v=byteleft(a,(unsigned)count);break;default:abort();}
  _mm_storeu_si128((__m128i *)result,v);expected_bits(op,in,count,want);assert(!memcmp(result,want,16));++total;
 }
 printf("PASS: %u x86 packed shift model comparisons (Rosetta host)\n",total);return 0;}
#else
#include "../port/llasm-support/llasm_cpu.h"
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "moves.inc"
static uint32_t addr(_cpu *c,const move_case *m){uint32_t g[8];memcpy(g,c,32);return m->disp+(m->base<0?0:g[m->base])+(m->index<0?0:g[m->index]*m->scale);}
static uint32_t prepare(_cpu *c,unsigned r,unsigned sample,const move_case *m){uint32_t seed=0x9ed713+r*257+sample;for(unsigned i=0;i<sizeof *c;++i)((unsigned char *)c)[i]=(unsigned char)next(&seed);uint32_t g[8]={0x1000,0x1010,0x1020,0x1030,0x1040,0x1050,0x1060,0x1070};memcpy(c,g,32);uint32_t a=addr(c,m);if(m->kind==2&&m->base>=0){assert(m->base!=m->index);g[m->base]-=a&15;memcpy(c,g,32);a=addr(c,m);}assert(m->kind!=2||!(a&15));return a;}
static void expected(_cpu *c,_cpu *after,const move_case *m,const unsigned char *mem){uint64_t count=m->count;if(m->kind)memcpy(&count,m->kind==2?mem:c->_xmm[m->source],8);*after=*c;expected_bits(m->op,c->_xmm[m->dest],count,after->_xmm[m->dest]);}
int main(void){unsigned long total=0;unsigned guards=0,faults=0;unsigned char arena[64],before_memory[64];size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(0,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED&&!mprotect(guard+page,page,PROT_NONE));
 for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *m=&moves[r];
  for(unsigned sample=0;sample<64;++sample){_cpu c,after;uint32_t a=prepare(&c,r,sample,m),seed=sample+r+1;uint64_t count=sample<sizeof counts/sizeof counts[0]?counts[sample]:sample<48?sample:((uint64_t)next(&seed)<<32)|next(&seed);unsigned char *mem=arena+16+sample%8;memset(arena,0xa6,sizeof arena);for(unsigned k=0;k<16;++k)mem[k]=(unsigned char)next(&seed);memcpy(mem,&count,8);
   if(sample<16){memset(c._xmm[m->dest],sample%2?0xff:0,16);c._xmm[m->dest][sample]^=0x80;}
   if(m->kind==1)memcpy(c._xmm[m->source],&count,8);
   c._pointer_offset=(uintptr_t)mem-a;memcpy(before_memory,arena,64);expected(&c,&after,m,mem);bodies[r](&c);assert(!memcmp(&c,&after,sizeof c)&&!memcmp(arena,before_memory,64));++total;
  }
  if(m->kind==2){_cpu c,after;uint32_t a=prepare(&c,r,0,m);c._pointer_offset=(uintptr_t)(guard+page-16)-a;memset(guard+page-16,0x5a,16);uint64_t count=7;memcpy(guard+page-16,&count,8);unsigned char original[16];memcpy(original,guard+page-16,16);expected(&c,&after,m,original);bodies[r](&c);assert(!memcmp(&c,&after,sizeof c)&&!memcmp(guard+page-16,original,16));++guards;}
 }
 _cpu *shared=mmap(0,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);unsigned seen[9]={0};
 for(unsigned r=0;r<sizeof moves/sizeof moves[0];++r){const move_case *m=&moves[r];if(m->kind!=2||m->base<0||m->base==m->index||seen[m->op]++)continue;
  for(unsigned kind=0;kind<17;++kind){uint32_t a=prepare(shared,r,0,m);unsigned char *mem=kind<15?arena:guard+page-(kind==16?8:0);if(kind<15){uint32_t g[8];memcpy(g,shared,32);g[m->base]+=kind+1;memcpy(shared,g,32);a=addr(shared,m);}else if(kind==16)memset(mem,0,8);
   shared->_pointer_offset=(uintptr_t)mem-a;memset(shared->_xmm[m->dest],0,16);_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[r](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status));assert(kind<15?WTERMSIG(status)==SIGABRT:(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));++faults;
  }
 }
 assert(!munmap(shared,page)&&!munmap(guard,page*2));printf("PASS: %lu emitted packed shift cases; %u protected16byte reads; %u host faults; full CPU preservation\n",total,guards,faults);return 0;}
#endif
