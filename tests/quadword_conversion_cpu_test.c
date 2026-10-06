#include "../port/llasm-support/llasm_cpu.h"
#include "../port/llasm-support/tribes_sse.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "cases.inc"
static void expected(_cpu *wanted,unsigned dest,const struct conversion_case *v) {
    wanted->_mxcsr|=v->raised;memcpy(wanted->_xmm[dest],v->output,16);memset(wanted->_ymm_hi[dest],0,16);
}
int main(void) {
 unsigned long total=0;unsigned char arena[96],saved[96];
 for(unsigned c=0;c<sizeof cases/sizeof cases[0];++c) {
  const struct conversion_case *v=&cases[c];
  for(unsigned dest=0;dest<8;++dest)for(unsigned src=0;src<8;++src) {
   _cpu cpu;memset(&cpu,(unsigned char)(c+dest+src),sizeof cpu);cpu._mxcsr=v->control;
   memcpy(cpu._xmm[src],v->input,16);_cpu wanted=cpu;expected(&wanted,dest,v);
   tb_evex_quadconv128(&cpu,v->op,dest,src);assert(!memcmp(&cpu,&wanted,sizeof cpu));++total;
  }
  for(unsigned alignment=0;alignment<16;++alignment) {
   memset(arena,0xa6,sizeof arena);memcpy(arena+17+alignment,v->input,v->op<=1?8:16);memcpy(saved,arena,sizeof arena);
   _cpu cpu;memset(&cpu,(unsigned char)c,sizeof cpu);cpu._pointer_offset=(uintptr_t)arena;cpu._mxcsr=v->control;
   _cpu wanted=cpu;unsigned dest=alignment%8;expected(&wanted,dest,v);
   tb_evex_quadconv128mem(&cpu,v->op,dest,17+alignment);assert(!memcmp(&cpu,&wanted,sizeof cpu));assert(!memcmp(arena,saved,sizeof arena));++total;
  }
 }
 size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));unsigned char *copy=malloc(page);assert(copy);
 for(unsigned c=0;c<sizeof cases/sizeof cases[0];++c) {
  const struct conversion_case *v=&cases[c];unsigned size=v->op<=1?8:16,dest=c%8;memset(guard,0xa6,page);memcpy(guard+page-size,v->input,size);memcpy(copy,guard,page);
  _cpu cpu;memset(&cpu,(unsigned char)c,sizeof cpu);cpu._pointer_offset=(uintptr_t)guard-0x1000;cpu._mxcsr=v->control;_cpu wanted=cpu;expected(&wanted,dest,v);
  tb_evex_quadconv128mem(&cpu,v->op,dest,0x1000+(uint32_t)page-size);assert(!memcmp(&cpu,&wanted,sizeof cpu));assert(!memcmp(guard,copy,page));++total;
 }
 assert(!munmap(guard,2*page));free(copy);
 /* Unmasked isolated IE/PE abort without publishing any vector bytes. Shared
    state records publication even when the child stops on SIGABRT. */
 unsigned exception_checks=0;
#ifdef TRIBESPAD_CPU_HOST_FAULT_TESTS
 _cpu *shared=mmap(NULL,sizeof(_cpu),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);
 for(unsigned which=0;which<2;++which) {
  memset(shared,0xa5,sizeof *shared);shared->_mxcsr=0x1f80&~(which?0x1000:0x80);
  memset(shared->_xmm[1],0,16);uint32_t bits=which?0x3f000000:0x7fc00001;memcpy(shared->_xmm[1],&bits,4);_cpu before=*shared;
  pid_t child=fork();assert(child>=0);
  if(!child){freopen("/dev/null","w",stderr);tb_evex_quadconv128(shared,1,0,1);_exit(3);}
  int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  before._mxcsr|=which?32:1;assert(!memcmp(shared,&before,sizeof before));
 }
 assert(!munmap(shared,sizeof(_cpu)));exception_checks=2;
#endif
 printf("PASS: %lu direct quadword CPU checks, full state and memory; %u isolated exception publication checks\n",total,exception_checks);return 0;
}
