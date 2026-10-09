/* Checked x87 CPU-object boundaries, guest pointees and independent threads. */
#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern void x87_fninit_void(CPU),x87_fld_float(CPU,uint32_t),x87_fadd_float(CPU,uint32_t);
extern void x87_fstp_tword(CPU,uint32_t),x87_fld_tword(CPU,uint32_t),x87_fld_st(CPU,int);
extern int32_t x87_fstp_float(CPU);
static unsigned char *memory;
static const size_t size=0x501000;
static void *worker(void *unused){
 (void)unused;
 for(unsigned i=0;i<2000;i++){
  _cpu cpu={0};x87_fninit_void(&cpu);
  x87_fld_float(&cpu,0x3f800000);x87_fadd_float(&cpu,0x3f800000);
  assert((uint32_t)x87_fstp_float(&cpu)==0x40000000);
  assert(cpu._st_top==0&&cpu._st_tag==0xffff);
 }
 return NULL;
}
int main(int argc,char **argv){
 int optimized=argc==2&&!strcmp(argv[1],"optimized");
 memory=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(tribes_memory_checks_configure((uintptr_t)memory,size));
 _cpu cpu={0};cpu._pointer_offset=(uintptr_t)memory;
 x87_fninit_void(&cpu);x87_fld_float(&cpu,0x3f800000);x87_fstp_tword(&cpu,32);
 assert(memory[39]==0x80&&memory[40]==0xff&&memory[41]==0x3f);
 x87_fld_tword(&cpu,32);assert((uint32_t)x87_fstp_float(&cpu)==0x3f800000);
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
 unsigned char *before=malloc(size);assert(before);unsigned faults=0;
 for(unsigned kind=0;kind<(optimized?5u:3u);kind++){
  memset(memory,0xa5,size);memcpy(before,memory,size);pid_t child=fork();assert(child>=0);
  if(!child){
   _cpu c={0};c._pointer_offset=(uintptr_t)memory;x87_fninit_void(&c);x87_fld_float(&c,0x3f800000);
   if(kind==0){assert(tribes_memory_permissions_set(0,4096,1));x87_fstp_tword(&c,32);}
   if(kind==1){assert(tribes_memory_permissions_set(0,4096,0));x87_fld_tword(&c,32);}
   if(kind==2)x87_fld_st(&c,8);
   if(kind==3)x87_fninit_void((_cpu *)(memory+32));
   if(kind==4)x87_fninit_void((_cpu *)((uintptr_t)memory-32));
   _exit(90);
  }
  int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  assert(!memcmp(before,memory,size));faults++;
 }
 printf("PASS: x87 guest load/store, 16000 independent threaded arithmetic cases, %u pre-access CPU/guest/index faults preserve shared memory\n",faults);
 return 0;
}
