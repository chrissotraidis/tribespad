#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern void bytecas_0(_cpu *); /* original LOCK CMPXCHG [esi],cl */
static unsigned char *memory;
static unsigned total;
static void *worker(void *unused) {
 (void)unused;_cpu c={0};c._pointer_offset=(uintptr_t)memory;c._esi=4097;c._ecx=1;c._eflags=0x202;
 for(unsigned i=0;i<10000;i++) {
  do {c._eax=0;bytecas_0(&c);}while(!(c._eflags&0x40));
  total++;
  __atomic_store_n(memory+4097,0,__ATOMIC_SEQ_CST);
 }
 return NULL;
}
static void fault(_cpu *c,unsigned fault) {
   c->_esi=4097;c->_eax=fault==1?1:0;c->_ecx=7;
   if(fault<2)assert(tribes_memory_permissions_set(4096,4096,1));
   if(fault==2)assert(tribes_memory_permissions_set(4096,4096,0));
   if(fault==3)assert(tribes_memory_permissions_set(4096,4096,2));
   if(fault==4){unsigned char expected=0;TribesPublishedCodeRange r={4097,1,&expected};assert(tribes_memory_publish_code((uintptr_t)memory,&r,1));}
   bytecas_0(c);_Exit(99);
}
int main(int argc,char **argv) {
 (void)argv;memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);memset(memory,0xa5,65536);memory[4097]=0;
 assert(tribes_memory_checks_configure((uintptr_t)memory,65536));
 if(argc==2&&argv[1][0]=='f'){_cpu c={0};c._pointer_offset=(uintptr_t)memory;unsigned n=(unsigned)strtoul(argv[1]+1,NULL,10);assert(n<5);fault(&c,n);}
 pthread_t threads[8];
 for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
 assert(total==80000&&memory[4097]==0&&memory[4096]==0xa5&&memory[4098]==0xa5);
 /* A byte at the final guest address is valid and touches no next page. */
 _cpu c={0};c._pointer_offset=(uintptr_t)memory;c._esi=65535;c._eax=0x123456a5;c._ecx=0x89abcdef;c._eflags=0x202;bytecas_0(&c);assert(c._eax==0x123456a5&&memory[65535]==0xef&&(c._eflags&0x40));
 c._eax=0x12345600;bytecas_0(&c);assert(c._eax==0x123456ef&&memory[65535]==0xef&&!(c._eflags&0x40));
 unsigned char snapshot[65536];memcpy(snapshot,memory,sizeof snapshot);
 if(argc==1)for(unsigned which=0;which<5;which++) {
  int channel[2];assert(!pipe(channel));pid_t pid=fork();assert(pid>=0);
  if(!pid) {
   struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);close(channel[0]);assert(dup2(channel[1],2)==2);close(channel[1]);
   fault(&c,which);
  }
  close(channel[1]);char text[2048];size_t used=0;ssize_t n;while((n=read(channel[0],text+used,sizeof text-1-used))>0){used+=(size_t)n;assert(used<sizeof text-1);}text[used]=0;close(channel[0]);int status;assert(waitpid(pid,&status,0)==pid&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(strstr(text,"checked guest memory violation"));assert(!memcmp(snapshot,memory,sizeof snapshot));
 }
 puts("PASS: 80000 byte-CAS protected increments, odd/final-byte addresses, preserved neighbors and upper EAX, and five no-mutation read/write permission and publication faults when enabled");return 0;
}
