#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
_Thread_local uint32_t external_tls;
extern uint32_t host_locals(uint32_t *,uint32_t);
extern void host_pointee(uint32_t *,uint32_t);
static unsigned char *guest;
static void *worker(void *arg){
 unsigned index=(unsigned)(uintptr_t)arg;uint32_t *p=(uint32_t *)(guest+index*4096);
 for(uint32_t n=0;n<10000;n++){
  *p=n;assert(host_locals(p,n)==(n^0x12345678u));assert(*p==n+1);assert(external_tls==n);
  host_pointee(p,n+2);assert(*p==n+2);
 }
 return NULL;
}
int main(int argc,char **argv){
 (void)argv;guest=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(guest!=MAP_FAILED);
 assert(tribes_memory_checks_configure((uintptr_t)guest,65536));
 if(argc==1){
  for(unsigned test=0;test<4;test++){
   uint32_t *p=(uint32_t *)guest;*p=0xdeadbeef;
   assert(tribes_memory_permissions_set(0,4096,test==0?0:1));
   pid_t pid=fork();assert(pid>=0);
   if(!pid){freopen("/dev/null","w",stderr);if(test<2)host_locals(p,17);else if(test==2)host_pointee(p,19);else host_locals((uint32_t *)(guest+65534),23);_exit(0);}
   int status;assert(waitpid(pid,&status,0)==pid);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(*p==0xdeadbeef);
  }
  assert(tribes_memory_permissions_set(0,4096,3));
 }
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
 puts("PASS: native stack/TLS values, checked guest pointees, eight independent threads; four pre-write guest faults in normal mode");
 return 0;
}
