#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
static void probe(unsigned op,unsigned memory,unsigned category,unsigned alias) {
 unsigned dest=op==2?1:alias?2:1,block=0,found=0;
 for(;block<sizeof moves/sizeof moves[0];block++) {
  const move_case *c=&moves[block];
  if(c->op==op&&c->kind==memory&&c->dest==dest&&(op==2||c->merge==1)&&(memory?c->address==1:c->source==2)){found=1;break;}
 }
 assert(found);
 unsigned char *shared=mmap(NULL,sizeof(_cpu)+97,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);memset(shared,0xa5,sizeof(_cpu)+97);_cpu *cpu=(_cpu *)shared;
 uint64_t left,right;
 if(op==2) {
  left=(uint64_t[]){UINT64_C(0x7ff812345678abcd),UINT64_C(0x3ff0000000000000),1,UINT64_C(0x7ff812345678abcd),UINT64_C(0x7ff812345678abcd),UINT64_C(0x8000000000000000),0,UINT64_C(0x0010000000000000),1}[category];
  right=(uint64_t[]){1,UINT64_C(0x7ff012345678abcd),UINT64_C(0x3ff0000000000000),1,1,0,UINT64_C(0x8000000000000000),1,UINT64_C(0x7ff012345678abcd)}[category];
 } else {
  left=(uint32_t[]){0x7fc12345,0x3f800000,1,0x7fc12345,0x7fc12345,0x80000000,0,0x00800000,1}[category];
  right=(uint32_t[]){1,0x7f812345,0x3f800000,1,1,0,0x80000000,1,0x7f812345}[category];
 }
 cpu->_mxcsr=(unsigned[]){0x1f00,0x1f00,0x1e80,0x1e80,0x1ec0,0x3f,0x3f,0x40,0x1e80}[category];
 unsigned raised=(unsigned[]){1,1,2,1,1,0,0,0,1}[category],n=op==2?8:4;
 memcpy(cpu->_xmm[1],&left,n);memcpy(cpu->_xmm[2],&right,n);memcpy(shared+sizeof *cpu+17,&right,n);
 cpu->_pointer_offset=(uintptr_t)(shared+sizeof *cpu+17)-0x200000;cpu->_esp=0x200000-16;
 _cpu expected=*cpu;expected._mxcsr|=raised;
 if(category>=3) {
  if(op==2)expected._eflags=(expected._eflags&~0x8d5u)|((unsigned[]){0x45,0x45,1,0x45,0x45,0x40,0x40,0,0x45}[category]);
  else {
   uint32_t result=(unsigned[]){1,0x7f812345,0,1,0,0,0x80000000,op==1?0x00800000u:0u,0x7f812345}[category];
   unsigned char value[16];memcpy(value,cpu->_xmm[1],16);memcpy(value,&result,4);memcpy(expected._xmm[dest],value,16);memset(expected._ymm_hi[dest],0,16);
  }
 }
 unsigned char wanted[97];memcpy(wanted,shared+sizeof *cpu,97);pid_t child=fork();assert(child>=0);
 if(!child){bodies[block](cpu);_exit(0);}
 int status;assert(waitpid(child,&status,0)==child);
 if(category<3)assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);else assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 assert(!memcmp(cpu,&expected,sizeof *cpu)&&!memcmp(shared+sizeof *cpu,wanted,97));assert(!munmap(shared,sizeof(_cpu)+97));
}
int main(void) {
 for(unsigned op=0;op<3;op++)for(unsigned memory=0;memory<2;memory++)for(unsigned category=0;category<9;category++)for(unsigned alias=0;alias<(op==2?1:2);alias++)probe(op,memory,category,alias);
 puts("PASS: 90 emitted scalar MIN/MAX/COMISD probes; unmasked QNaN/SNaN invalid and denormal preserve destination/flags, masked invalid suppresses DE, DAZ/tie/source-two payload and prior-sticky controls; full CPU/guarded memory; SIGABRT boundary, not Windows fault delivery");return 0;
}
