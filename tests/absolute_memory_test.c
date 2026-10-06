#include "../port/llasm-support/tribes_absolute_memory.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned char *memory;
static void *read_thread(void *arg) {
 assert(tribes_absolute_memory_bind((uintptr_t)arg));
 for(unsigned i=0;i<2000;i++)assert(x86_mov_reg_mem_32(60)==0x12345678);
 tribes_absolute_memory_clear();return NULL;
}
static void *unbound_thread(void *arg) {(void)arg;(void)x86_mov_reg_mem_8(0);return NULL;}
int main(void) {
 uint64_t size=UINT64_C(0x100000000);
 memory=mmap(NULL,size,PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED&&(uintptr_t)memory>UINT32_MAX);
 assert(!mprotect(memory,65536,PROT_READ|PROT_WRITE));
 assert(!mprotect(memory+size-16384,16384,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure((uintptr_t)memory,size));
 tribes_memory_external_configure((uintptr_t)memory,size);tribes_memory_external_enable();
 assert(tribes_absolute_memory_bind((uintptr_t)memory));
 assert(!tribes_absolute_memory_bind(0)&&!tribes_absolute_memory_bind(UINT64_MAX));
 memset(memory,0xa5,65536);
 uint32_t patterns[]={0,1,0x12345678,0x80000000,0xffffffff};unsigned cases=0;
 for(unsigned offset=0;offset<8;offset++)for(unsigned i=0;i<5;i++) {
  uint32_t v=patterns[i],a=16384+offset;unsigned char snapshot[65536];
  memcpy(snapshot,memory,65536);x86_mov_mem_reg_8(a,v);snapshot[a]=(uint8_t)v;
  assert(x86_mov_reg_mem_8(a)==(uint8_t)v&&!memcmp(snapshot,memory,65536));
  x86_mov_mem_reg_16(a,v);snapshot[a]=(uint8_t)v;snapshot[a+1]=(uint8_t)(v>>8);
  assert(x86_mov_reg_mem_16(a)==(uint16_t)v&&!memcmp(snapshot,memory,65536));
  x86_mov_mem_reg_32(a,v);for(unsigned j=0;j<4;j++)snapshot[a+j]=(uint8_t)(v>>(j*8));
  assert(x86_mov_reg_mem_32(a)==v&&!memcmp(snapshot,memory,65536));cases+=3;
 }
 x86_mov_mem_reg_32(4094,0x12345678);assert(x86_mov_reg_mem_32(4094)==0x12345678);
 x86_mov_mem_reg_32(0xfffffffc,0x12345678);assert(x86_mov_reg_mem_32(0xfffffffc)==0x12345678);
 x86_mov_mem_reg_8(0xffffffff,0xaa);assert(x86_mov_reg_mem_8(0xffffffff)==0xaa);
 x86_mov_mem_reg_32(60,0x12345678);pthread_t threads[4];
 for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,read_thread,memory));
 for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 assert(x86_mov_reg_mem_32(60)==0x12345678); /* child clear cannot clear parent */
 assert(tribes_memory_permissions_set(4096,4096,1));
 assert(tribes_memory_permissions_set(8192,4096,0));
 unsigned char snapshot[65536];memcpy(snapshot,memory,65536);
 for(unsigned i=0;i<9;i++) {
  int fd[2];assert(!pipe(fd));pid_t child=fork();assert(child>=0);
  if(!child) {
   struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);close(fd[0]);assert(dup2(fd[1],2)==2);close(fd[1]);
   if(i==0)x86_mov_mem_reg_32(4094,0);
   if(i==1)x86_mov_mem_reg_16(4095,0);
   if(i==2)x86_mov_mem_reg_8(4096,0);
   if(i==3)(void)x86_mov_reg_mem_32(8190);
   if(i==4)x86_mov_mem_reg_32(8192,0);
   if(i==5)(void)x86_mov_reg_mem_32(0xfffffffd);
   if(i==6)x86_mov_mem_reg_16(0xffffffff,0);
   if(i==7){tribes_absolute_memory_clear();(void)x86_mov_reg_mem_8(0);}
   if(i==8){pthread_t t;assert(!pthread_create(&t,NULL,unbound_thread,NULL));pthread_join(t,NULL);}
   _exit(99);
  }
  close(fd[1]);char message[1024];ssize_t n=read(fd[0],message,sizeof message-1);assert(n>0);message[n]=0;close(fd[0]);
  int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  assert(strstr(message,i<5?"checked guest memory violation":"invalid absolute guest memory access"));
  assert(!memcmp(snapshot,memory,65536));
 }
 printf("PASS: %u absolute-memory width/alignment/value cases; full 32-bit guest bounds, cross-page access, 8000 thread reads, nine fault cases and complete failed-write preservation\n",cases);
 return 0;
}
