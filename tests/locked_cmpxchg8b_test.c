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
extern void xadd_0(_cpu *); /* emitted LOCK CMPXCHG8B [edi] */
#define THREADS 8
#define COUNT 10000
#define INITIAL UINT64_C(0xffffff00)
static unsigned char *memory;
static uint64_t results[THREADS*COUNT];
static uint64_t accumulator(_cpu *c){return ((uint64_t)c->_edx<<32)|c->_eax;}
static void set_acc(_cpu *c,uint64_t value){c->_eax=(uint32_t)value;c->_edx=(uint32_t)(value>>32);}
static void set_desired(_cpu *c,uint64_t value){c->_ebx=(uint32_t)value;c->_ecx=(uint32_t)(value>>32);}
static void *worker(void *arg){size_t i=(size_t)arg;_cpu c={0};c._pointer_offset=(uintptr_t)memory;c._edi=4096;c._eflags=0xa97;for(unsigned j=0;j<COUNT;j++){do{set_desired(&c,accumulator(&c)+1);xadd_0(&c);assert((c._eflags&~0x40u)==(0xa97u&~0x40u));}while(!(c._eflags&0x40));results[i*COUNT+j]=accumulator(&c);}return NULL;}
static int compare(const void *a,const void *b){uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return (x>y)-(x<y);}
int main(int argc,char **argv){(void)argv;memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_ANON|MAP_SHARED,-1,0);assert(memory!=MAP_FAILED);uint64_t value=INITIAL;memcpy(memory+4096,&value,8);assert(tribes_memory_checks_configure((uintptr_t)memory,65536));
 pthread_t threads[THREADS];for(size_t i=0;i<THREADS;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<THREADS;i++)assert(!pthread_join(threads[i],NULL));memcpy(&value,memory+4096,8);assert(value==INITIAL+THREADS*COUNT);qsort(results,THREADS*COUNT,sizeof(uint64_t),compare);for(unsigned i=0;i<THREADS*COUNT;i++)assert(results[i]==INITIAL+i);
 assert(tribes_memory_permissions_set(4096,4096,1));unsigned char snapshot[65536];memcpy(snapshot,memory,65536);
 if(argc==1)for(unsigned n=0;n<4;n++){int channel[2];assert(!pipe(channel));pid_t pid=fork();assert(pid>=0);if(!pid){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);close(channel[0]);assert(dup2(channel[1],2)==2);close(channel[1]);_cpu c={0};c._pointer_offset=(uintptr_t)memory;c._edi=n<2?4096:n==2?8192:4092;set_acc(&c,n==0?value:0);set_desired(&c,7);if(n==2)assert(tribes_memory_permissions_set(8192,4096,0));xadd_0(&c);_Exit(99);}close(channel[1]);char message[1024];ssize_t bytes=read(channel[0],message,sizeof message-1);assert(bytes>0);message[bytes]=0;close(channel[0]);int status;assert(waitpid(pid,&status,0)==pid&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(strstr(message,n==3?"unsupported unaligned":"checked guest memory violation"));assert(!memcmp(snapshot,memory,65536));}
 assert(tribes_memory_permissions_set(4096,4096,3));_cpu c={0};c._pointer_offset=(uintptr_t)memory;c._edi=4096;set_desired(&c,7);xadd_0(&c);assert(accumulator(&c)==value&&!(c._eflags&0x40));set_desired(&c,value+1);xadd_0(&c);assert(accumulator(&c)==value&&(c._eflags&0x40));uint64_t updated;memcpy(&updated,memory+4096,8);assert(updated==value+1);puts("PASS: 80000 contended emitted CMPXCHG8B increments cross the low-word carry and return each prior64-bit value once; only ZF changes; restored success/failure and isolated write-required/no-access/unaligned faults when enabled");return 0;}
