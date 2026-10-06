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
/* Both are emitted original instruction bodies: CMPXCHG [edx],ecx and
 * XADD [edx],ecx. They must interoperate on the same guest counter. */
extern void xadd_0(_cpu *),xadd_1(_cpu *);
#define THREADS 8
#define COUNT 10000
static unsigned char *memory;
static uint32_t results[THREADS*COUNT];
static void *worker(void *arg){size_t i=(size_t)arg;_cpu c={0};c._pointer_offset=(uintptr_t)memory;c._edx=4096;c._eflags=0x202;for(unsigned j=0;j<COUNT;j++){if(i&1){c._ecx=1;xadd_1(&c);results[i*COUNT+j]=c._ecx;}else{do{c._ecx=c._eax+1;xadd_0(&c);}while(!(c._eflags&0x40));results[i*COUNT+j]=c._eax;}}return NULL;}
static int compare(const void *a,const void *b){uint32_t x=*(const uint32_t *)a,y=*(const uint32_t *)b;return (x>y)-(x<y);}
int main(int argc,char **argv){(void)argv;memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_ANON|MAP_SHARED,-1,0);assert(memory!=MAP_FAILED);assert(tribes_memory_checks_configure((uintptr_t)memory,65536));
 pthread_t threads[THREADS];for(size_t i=0;i<THREADS;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<THREADS;i++)assert(!pthread_join(threads[i],NULL));assert(*(uint32_t *)(memory+4096)==THREADS*COUNT);qsort(results,THREADS*COUNT,sizeof(uint32_t),compare);for(unsigned i=0;i<THREADS*COUNT;i++)assert(results[i]==i);
 assert(tribes_memory_permissions_set(4096,4096,1));unsigned char snapshot[65536];memcpy(snapshot,memory,65536);
 if(argc==1)for(unsigned n=0;n<4;n++){int channel[2];assert(!pipe(channel));pid_t pid=fork();assert(pid>=0);if(!pid){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);close(channel[0]);assert(dup2(channel[1],2)==2);close(channel[1]);_cpu c={0};c._pointer_offset=(uintptr_t)memory;c._edx=n<2?4096:n==2?8192:4095;c._eax=n==0?THREADS*COUNT:0;c._ecx=7;if(n==2)assert(tribes_memory_permissions_set(8192,4096,0));xadd_0(&c);_Exit(99);}close(channel[1]);char message[1024];ssize_t bytes=read(channel[0],message,sizeof message-1);assert(bytes>0);message[bytes]=0;close(channel[0]);int status;assert(waitpid(pid,&status,0)==pid&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(strstr(message,n==3?"unsupported unaligned":"checked guest memory violation"));assert(!memcmp(snapshot,memory,65536));}
 assert(tribes_memory_permissions_set(4096,4096,3));_cpu c={0};c._pointer_offset=(uintptr_t)memory;c._edx=4096;c._eax=0;c._ecx=7;xadd_0(&c);assert(c._eax==THREADS*COUNT&&!(c._eflags&0x40)&&*(uint32_t *)(memory+4096)==THREADS*COUNT);c._ecx=THREADS*COUNT+1;xadd_0(&c);assert(c._eax==THREADS*COUNT&&(c._eflags&0x40)&&*(uint32_t *)(memory+4096)==THREADS*COUNT+1);puts("PASS: 80000 mixed emitted CAS/XADD contended increments return each prior value once; successful and failed compare on restored permissions; isolated read-only success/failure, no-access and unaligned faults when enabled");return 0;}
