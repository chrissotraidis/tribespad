#include "../port/core/native_handles.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;
static uint32_t watched;
extern void completion_watch(uint32_t);
extern int completion_observed(void);
static uint32_t word(uint32_t at){uint32_t n;memcpy(&n,memory+at,4);return n;}
static uint32_t call(unsigned target,const uint32_t *args,size_t count){
 _cpu cpu;memset(&cpu,0xa9,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x18003;memcpy(memory+cpu._esp,args,count*4);_cpu expected=cpu;
 tribes_enter(&cpu,target);expected._eax=cpu._eax;expected._esp+=(uint32_t)count*4;
 assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0x18003,args,count*4));return cpu._eax;
}
static void *wake(void *arg){
 while(!completion_observed())sched_yield();uint32_t h=watched;
 if(arg)assert(tribes_close_handle(h));else assert(tribes_post_queued_completion(h,17,23,0xdeadbeef));return NULL;
}
typedef struct Producer {uint32_t port,id;} Producer;
static void *producer(void *arg){Producer *p=arg;for(uint32_t i=0;i<2000;i++)assert(tribes_post_queued_completion(p->port,i,p->id,0x80000000|i));return NULL;}
static void *second_consumer(void *arg){tribes_get_queued_completion(base,(uint32_t)(uintptr_t)arg,0x11001,0x11009,0x11011,0);return NULL;}
static void rejected(unsigned kind,uint32_t port){
 pid_t child=fork();assert(child>=0);if(!child){
  if(kind==0)tribes_create_io_completion_port(base,1,port,0,1);
  else if(kind==1)tribes_create_io_completion_port(base,UINT32_MAX,0,0,2);
  else if(kind==2){pthread_t thread;assert(!pthread_create(&thread,NULL,second_consumer,(void *)(uintptr_t)port));pthread_join(thread,NULL);}
  else if(kind==3)tribes_duplicate_handle(base,UINT32_MAX,port,UINT32_MAX,0x12001,0,0,2);
  else if(kind==5)tribes_get_queued_completion_ex(base,port,0x11001,128,0x12003,0,1);
  else if(kind==6){assert(tribes_post_queued_completion(port,1,2,3));assert(tribes_memory_permissions_set(0x11000,4096,1));tribes_get_queued_completion_ex(base,port,0x11001,128,0x12003,0,0);}
  else{assert(tribes_memory_permissions_set(0x11000,4096,1));tribes_get_queued_completion(base,port,0x11001,0x11009,0x11011,0);}
  _exit(90);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x10000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));
#ifdef CHECKED_TEST
 tribes_memory_external_enable();
#endif

 tribes_set_last_error(919);uint32_t create[]={UINT32_MAX,0,0x87654321,1};uint32_t port=call(1,create,4);assert(port&&tribes_get_last_error()==919);
 assert(tribes_fd_borrow(port)==-1&&tribes_get_last_error()==6);tribes_set_last_error(919);
 for(unsigned i=0;i<32;i++){uint32_t post[]={port,i,0xffffffff-i,i?0xdead0000+i:0};assert(call(2,post,4)==1&&tribes_get_last_error()==919);}
 uint32_t get[]={port,0x11001,0x11009,0x11011,0};
 for(unsigned i=0;i<32;i++){assert(call(3,get,5)==1&&tribes_get_last_error()==919);assert(word(0x11001)==i&&word(0x11009)==0xffffffff-i&&word(0x11011)==(i?0xdead0000+i:0));}
 memset(memory+0x11000,0xa5,64);assert(!call(3,get,5)&&tribes_get_last_error()==258&&word(0x11011)==0&&word(0x11001)==0xa5a5a5a5&&word(0x11009)==0xa5a5a5a5);
 get[4]=15;struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);assert(!call(3,get,5)&&tribes_get_last_error()==258);clock_gettime(CLOCK_MONOTONIC,&b);double elapsed=(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9;assert(elapsed>=0.01&&elapsed<5);
 /* One queue, two dequeue APIs; exact16-byte i686 entries and bounded batches. */
 uint32_t batch[]={port,0x11001,128,0x12003,0,0};
 for(unsigned i=0;i<259;i++)assert(tribes_post_queued_completion(port,i,0x80000000+i,i?0xffff0000+i:0));
 for(unsigned pass=0;pass<3;pass++){
  memset(memory+0x11000,0xa5,4096);tribes_set_last_error(919);assert(call(5,batch,6)&&tribes_get_last_error()==919);
  unsigned count=pass<2?128:3;assert(word(0x12003)==count);
  for(unsigned i=0;i<count;i++){unsigned n=pass*128+i;assert(word(0x11001+i*16)==0x80000000+n&&word(0x11005+i*16)==(n?0xffff0000+n:0)&&word(0x11009+i*16)==0&&word(0x1100d+i*16)==n);}
  assert(memory[0x11000]==0xa5&&memory[0x11001+count*16]==0xa5);
 }
 memset(memory+0x11000,0xa5,4096);assert(!call(5,batch,6)&&tribes_get_last_error()==258&&word(0x12003)==0&&word(0x11001)==0xa5a5a5a5);
 assert(tribes_post_queued_completion(port,9,8,7));batch[2]=0;assert(!call(5,batch,6)&&tribes_get_last_error()==87);batch[2]=UINT32_MAX;assert(!call(5,batch,6)&&tribes_get_last_error()==87);batch[2]=128;
 batch[1]=UINT32_MAX-7;assert(!call(5,batch,6)&&tribes_get_last_error()==87);batch[1]=0x11001;
 assert(call(3,get,5)&&word(0x11001)==9&&word(0x11009)==8&&word(0x11011)==7);
 batch[4]=15;clock_gettime(CLOCK_MONOTONIC,&a);assert(!call(5,batch,6)&&tribes_get_last_error()==258&&word(0x12003)==0);clock_gettime(CLOCK_MONOTONIC,&b);elapsed=(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9;assert(elapsed>=0.01&&elapsed<5);batch[4]=UINT32_MAX;
 pthread_t worker;watched=port;completion_watch(port);assert(!pthread_create(&worker,NULL,wake,NULL));get[4]=UINT32_MAX;tribes_set_last_error(919);assert(call(3,get,5)==1&&tribes_get_last_error()==919&&word(0x11001)==17&&word(0x11009)==23&&word(0x11011)==0xdeadbeef);assert(!pthread_join(worker,NULL));
 watched=port;completion_watch(port);assert(!pthread_create(&worker,NULL,wake,NULL));tribes_set_last_error(919);assert(call(5,batch,6)&&tribes_get_last_error()==919&&word(0x12003)==1&&word(0x11001)==23&&word(0x11005)==0xdeadbeef&&word(0x11009)==0&&word(0x1100d)==17);assert(!pthread_join(worker,NULL));
 Producer producers[4];pthread_t threads[4];uint32_t next[4]={0};
 for(unsigned i=0;i<4;i++){producers[i]=(Producer){port,i};assert(!pthread_create(&threads[i],NULL,producer,&producers[i]));}
 for(unsigned total=0;total<8000;){assert(call(5,batch,6));unsigned count=word(0x12003);assert(count&&count<=128&&count<=8000-total);total+=count;for(unsigned i=0;i<count;i++){unsigned id=word(0x11001+i*16),sequence=word(0x1100d+i*16);assert(id<4&&sequence==next[id]++&&word(0x11005+i*16)==(0x80000000|sequence)&&word(0x11009+i*16)==0);}}
 for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL)&&next[i]==2000);
#if !__has_feature(thread_sanitizer)
 for(unsigned i=0;i<4;i++)rejected(i,port);rejected(5,port);
#ifdef CHECKED_TEST
 rejected(4,port);rejected(6,port);
#endif
#endif
 watched=port;completion_watch(port);assert(!pthread_create(&worker,NULL,wake,(void *)1));memset(memory+0x11000,0xa5,64);
 assert(!call(3,get,5)&&tribes_get_last_error()==735&&word(0x11011)==0&&word(0x11001)==0xa5a5a5a5&&word(0x11009)==0xa5a5a5a5);assert(!pthread_join(worker,NULL));
 assert(!tribes_post_queued_completion(port,0,0,0)&&tribes_get_last_error()==6);assert(!call(3,get,5)&&tribes_get_last_error()==6);assert(!call(4,&port,1)&&tribes_get_last_error()==6);
 port=call(1,create,4);assert(port&&tribes_post_queued_completion(port,1,2,3)&&call(4,&port,1));
 port=call(1,create,4);batch[0]=port;watched=port;completion_watch(port);assert(!pthread_create(&worker,NULL,wake,(void *)1));memset(memory+0x11000,0xa5,4096);
 assert(!call(5,batch,6)&&tribes_get_last_error()==735&&word(0x12003)==0&&word(0x11001)==0xa5a5a5a5);assert(!pthread_join(worker,NULL));
 assert(!call(5,batch,6)&&tribes_get_last_error()==6&&word(0x12003)==0);
 puts("PASS: owned completion queue, five emitted guest ABIs, FIFO/opaque values, 259 bounded/interleaved packets and8000 concurrent producer packets, single and batched timeout/wakeup/close abandonment, invalid/type checks and explicit unsupported scheduling/association/duplication gates");return 0;
}
