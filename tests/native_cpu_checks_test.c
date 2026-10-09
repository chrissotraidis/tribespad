#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
typedef union { uint64_t align; unsigned char bytes[440]; } NativeCPU;
extern uint32_t native_probe(void *,uint32_t *,uint64_t);
extern void native_outside(void *);
extern void native_unbounded(void *,uint64_t);
extern void native_pointee(void *);
static unsigned char *guest;
static void *worker(void *arg){
 unsigned id=(unsigned)(uintptr_t)arg;NativeCPU cpu={0};
 uint32_t *p=(uint32_t *)(guest+4096*id);
 for(unsigned n=0;n<10000;n++){
  *p=n;assert(native_probe(&cpu,p,n%8)==n);assert(*p==n+1);
  uint32_t value;memcpy(&value,cpu.bytes+176+16*(n%8),4);assert(value==n);
 }
 return NULL;
}
int main(int argc,char **argv){
 (void)argv;
 unsigned char *mapping=mmap(NULL,40960,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
 assert(mapping!=MAP_FAILED);guest=mapping+4096;
 assert(tribes_memory_checks_configure((uintptr_t)guest,32768));
 if(argc==1)for(unsigned test=0;test<10;test++){
  NativeCPU cpu={0};uint32_t *p=(uint32_t *)guest;*p=0x12345678;
  assert(tribes_memory_permissions_set(0,4096,test==4?0:1));
  uint64_t pointer=(uintptr_t)guest;memcpy(cpu.bytes+112,&pointer,8);
  pid_t child=fork();assert(child>=0);
  if(!child){
   freopen("/dev/null","w",stderr);
   switch(test){
    case 0:native_probe(NULL,p,0);break;
    case 1:native_probe(guest,p,0);break;
    case 2:native_probe(guest-439,p,0);break;
    case 3:native_probe((void *)(UINTPTR_MAX-438),p,0);break;
    case 4:case 5:native_probe(&cpu,p,0);break;
    case 6:native_outside(guest-440);break;
    case 7:native_unbounded(guest-440,440);break;
    case 8:native_pointee(&cpu);break;
    case 9:native_probe(&cpu,p,8);break;
   }
   _exit(0);
  }
  int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  assert(*p==0x12345678);
 }
 assert(tribes_memory_permissions_set(0,4096,3));
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));
 for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
 puts("PASS: indexed native CPU fields, preserved guest writes, eight threads; ten contract/guest faults in normal mode");
 return 0;
}
