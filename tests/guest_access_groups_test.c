#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern void batch(uint64_t *,uint64_t *);
extern void read_batch(const uint64_t *,uint64_t *);
extern uint64_t publication_boundary(uint64_t *);
static uint64_t *memory;
void revoke_page(void){assert(tribes_memory_permissions_set(0,4096,0));}
static void *publisher(void *unused){
 (void)unused;
 for(unsigned i=0;i<4000;++i)assert(tribes_memory_permissions_set(4096,4096,(i&1)?1:3));
 return NULL;
}
static void *reader(void *context){
 uint64_t local=0;uint64_t *p=memory+1024+(uintptr_t)context*16;
 for(unsigned i=0;i<2000;++i){batch(p,&local);read_batch(memory+512,p+1);assert(p[1]==0);}
 assert(*p==32000&&local==2000);return NULL;
}
static double now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char **argv){
 (void)argv;memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(tribes_memory_checks_configure((uintptr_t)memory,65536));
 uint64_t host=0;batch(memory,&host);assert(*memory==16&&host==1);
 // Both pointer orders are valid: the first access can be host-only.
 batch(&host,memory);assert(*memory==17&&host==17);
 if(argc==1){
  pid_t child=fork();assert(child>=0);
  if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);alarm(5);freopen("/dev/null","w",stderr);publication_boundary(memory);_exit(99);}
  int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  assert(*memory==17); // the denied read must not continue into its store
 }
 pthread_t writer,workers[4];assert(!pthread_create(&writer,NULL,publisher,NULL));
 for(uintptr_t i=0;i<4;++i)assert(!pthread_create(workers+i,NULL,reader,(void *)i));
 for(unsigned i=0;i<4;++i)assert(!pthread_join(workers[i],NULL));assert(!pthread_join(writer,NULL));
 double start=now();for(unsigned i=0;i<100000;++i)batch(memory,&host);double elapsed=now()-start;
 assert(*memory==1600017&&host==100017);
 printf("PASS: mixed host/guest groups, %u publication boundary fault, concurrent readers/writer; 100000 batches %.6f seconds\n",argc==1?1u:0u,elapsed);
}
