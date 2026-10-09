#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern void batch(uint64_t *,uint64_t *);
extern void host_prelude(uint64_t *),host_interleaved(uint64_t *);
extern void read_batch(const uint64_t *,uint64_t *);
extern uint64_t publication_boundary(uint64_t *);
static uint64_t *memory;
static atomic_int writer_started,writer_done;
static void *denying_writer(void *unused){
 (void)unused;atomic_store(&writer_started,1);
 assert(tribes_memory_permissions_set(0,4096,0));atomic_store(&writer_done,1);return NULL;
}
static void expect_bad_count(uint32_t count){
 pid_t child=fork();assert(child>=0);
 if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);alarm(5);freopen("/dev/null","w",stderr);tribes_memory_access_end_many(count);_exit(99);}
 int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
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
 host_prelude(memory+2048);assert(memory[2048]==4);
 host_interleaved(memory+2048);assert(memory[2048]==5);
 uint64_t host=0;batch(memory,&host);assert(*memory==16&&host==1);
 // Both pointer orders are valid: the first access can be host-only.
 batch(&host,memory);assert(*memory==17&&host==17);
 uint64_t host_only=0;batch(&host_only,&host);assert(host_only==16&&host==18);host--;
 // Ending a mixed group must leave the explicit outer bracket held even
 // after a writer has attempted to deny this page. All-host groups retire 0.
 uint32_t outer=tribes_memory_access_begin(memory,8,1);assert(outer==1);
 pthread_t denying;assert(!pthread_create(&denying,NULL,denying_writer,NULL));
 while(!atomic_load(&writer_started))usleep(100);
 usleep(20000);
 uint32_t count=tribes_memory_access_begin(memory,8,1)+tribes_memory_access_begin(&host,8,1);
 tribes_memory_access_end_many(count);assert(!atomic_load(&writer_done));
 assert(tribes_memory_access_begin(&host,8,1)==0);tribes_memory_access_end_many(0);
 if(argc==1){expect_bad_count(2);expect_bad_count(9);}
 tribes_memory_access_end(outer);assert(!pthread_join(denying,NULL));assert(atomic_load(&writer_done));
 assert(tribes_memory_permissions_set(0,4096,3));
 if(argc==1)expect_bad_count(1);
 if(argc==1){
  pid_t child=fork();assert(child>=0);
  if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);alarm(5);freopen("/dev/null","w",stderr);publication_boundary(memory);_exit(99);}
  int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  assert(*memory==17); // the denied read must not continue into its store
 }
 pthread_t writers[2],workers[4];
 for(unsigned i=0;i<2;++i)assert(!pthread_create(writers+i,NULL,publisher,NULL));
 for(uintptr_t i=0;i<4;++i)assert(!pthread_create(workers+i,NULL,reader,(void *)i));
 for(unsigned i=0;i<4;++i)assert(!pthread_join(workers[i],NULL));for(unsigned i=0;i<2;++i)assert(!pthread_join(writers[i],NULL));
 double start=now();for(unsigned i=0;i<100000;++i)batch(memory,&host);double elapsed=now()-start;
 assert(*memory==1600017&&host==100017);
 printf("PASS: mixed host/guest/host-only groups, nested pending writer, %u guard faults, concurrent readers/two publishers; 100000 batches %.6f seconds\n",argc==1?4u:0u,elapsed);
}
