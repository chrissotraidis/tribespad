#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/native_handles.h"
#include "../port/core/native_winsock.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;static _cpu cpu;
static uint32_t word(uint32_t at){uint32_t v;memcpy(&v,memory+at,4);return v;}
static uint32_t call(unsigned pc,const uint32_t *args,unsigned count){
 uint32_t top=cpu._esp;cpu._esp-=count*4;if(count)memcpy(memory+cpu._esp,args,count*4);_cpu saved=cpu;
 tribes_enter(&cpu,pc);saved._eax=cpu._eax;saved._esp=top;
 assert(!memcmp(&saved,&cpu,sizeof cpu)&&(!count||!memcmp(memory+top-count*4,args,count*4)));return cpu._eax;
}
static uint32_t make_port(void){uint32_t args[]={UINT32_MAX,0,0,1};return call(1,args,4);}
static uint32_t associate(uint32_t s,uint32_t port,uint32_t key){uint32_t a[]={s,port,key,0};return call(1,a,4);}
static uint32_t modes(uint32_t s,uint32_t flags){uint32_t a[]={s,flags};return call(11,a,2);}
static uint32_t dequeue(uint32_t port,uint32_t timeout){uint32_t a[]={port,0x22001,128,0x23003,timeout,0};memset(memory+0x22000,0xa5,4096);return call(5,a,6);}
typedef struct Endpoint {uint32_t token,key;int client,peer;unsigned number;} Endpoint;
static Endpoint endpoint(unsigned number){
 int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);struct sockaddr_in a={0};a.sin_len=sizeof a;a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 assert(!bind(listener,(void *)&a,sizeof a)&&!listen(listener,1));socklen_t size=sizeof a;assert(!getsockname(listener,(void *)&a,&size));
 uint32_t args[]={2,1,6};Endpoint e={.token=call(9,args,3),.number=number,.key=number?0xffffffff:0};assert(e.token!=UINT32_MAX);
 e.client=tribes_socket_duplicate(base,e.token);assert(e.client>=0&&!connect(e.client,(void *)&a,sizeof a));e.peer=accept(listener,NULL,NULL);assert(e.peer>=0&&!close(listener));return e;
}
static void *receive_and_post(void *arg){
 Endpoint *e=arg;for(unsigned i=0;i<64;i++){
  unsigned char sent=(unsigned char)(i+e->number),received=0;assert(send(e->peer,&sent,1,0)==1&&recv(e->client,&received,1,0)==1&&received==sent);
  assert(tribes_socket_post_completion(base,e->token,1,0x80000000|(e->number<<16)|i,0));
 }return NULL;
}
static void close_endpoint(Endpoint *e){assert(!close(e->client));assert(!call(10,&e->token,1));char b;assert(recv(e->peer,&b,1,0)==0);assert(!close(e->peer));}
typedef struct CloseRace {atomic_int start;uint32_t token,result,error;} CloseRace;
static void *race_post(void *arg){CloseRace *r=arg;while(!atomic_load(&r->start))sched_yield();tribes_set_last_error(919);r->result=tribes_socket_post_completion(base,r->token,1,0x5678,0);r->error=tribes_get_last_error();return NULL;}
static void close_races(void){
 for(unsigned i=0;i<32;i++){
  Endpoint e=endpoint(0);uint32_t port=make_port();assert(associate(e.token,port,0x1234)==port);
  char sent='x',received=0;assert(send(e.peer,&sent,1,0)==1&&recv(e.client,&received,1,0)==1&&received==sent);
  CloseRace r={.start=0,.token=e.token};pthread_t t;assert(!pthread_create(&t,NULL,race_post,&r));atomic_store(&r.start,1);
  if(i&1){
   assert(call(4,&port,1));assert(!pthread_join(t,NULL));assert(r.result?r.error==919:r.error==735);close_endpoint(&e);
  }else{
   close_endpoint(&e);assert(!pthread_join(t,NULL));assert(r.result?r.error==919:r.error==6);
   uint32_t result=dequeue(port,0);assert(result==r.result);
   if(result)assert(word(0x23003)==1&&word(0x22001)==0x1234&&word(0x22005)==0x5678&&word(0x2200d)==1);
   else assert(tribes_get_last_error()==258);
   assert(call(4,&port,1));
  }
 }
}
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x20000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));
#ifdef CHECKED
 tribes_memory_external_enable();
#endif
 memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x1fff1;
 uint32_t startup[]={0x101,0x20001};assert(!call(7,startup,2));Endpoint e[2]={endpoint(0),endpoint(1)};
 uint32_t port=make_port(),other=make_port();assert(port&&other);tribes_set_last_error(919);
 assert(associate(e[0].token,port,e[0].key)==port&&tribes_get_last_error()==919);
 assert(!associate(e[0].token,other,123)&&tribes_get_last_error()==87);
 assert(!associate(e[0].token,port,123)&&tribes_get_last_error()==87);
 assert(!tribes_socket_associate_completion(base+1,e[1].token,port,1)&&tribes_get_last_error()==6);
 assert(!associate(e[1].token,0xe100ffff,1)&&tribes_get_last_error()==6);
 assert(!associate(e[1].token,e[0].token,1)&&tribes_get_last_error()==6);
 assert(!modes(e[1].token,4)&&tribes_get_last_error()==87);tribes_set_last_error(919);
 assert(associate(e[1].token,port,e[1].key)==port&&tribes_get_last_error()==919);
 assert(modes(e[1].token,0x103)&&tribes_get_last_error()==919);assert(modes(e[1].token,0x100)&&tribes_get_last_error()==919);
 /* Only immediate successes are skipped; asynchronous completions still arrive. */
 assert(tribes_socket_post_completion(base,e[1].token,7,0x12345678,1));assert(!dequeue(port,0)&&tribes_get_last_error()==258&&word(0x23003)==0&&word(0x22001)==0xa5a5a5a5);
 /* Same queue, distinct retained keys, real TCP bytes and concurrent callbacks. */
 pthread_t threads[2];for(unsigned i=0;i<2;i++)assert(!pthread_create(&threads[i],NULL,receive_and_post,e+i));unsigned sequence[2]={0};
 for(unsigned total=0;total<128;){assert(dequeue(port,1000));unsigned count=word(0x23003);assert(count&&count<=128&&count<=128-total);total+=count;
  for(unsigned i=0;i<count;i++){uint32_t key=word(0x22001+16*i),ov=word(0x22005+16*i);unsigned which=(ov>>16)&1;
   assert(key==e[which].key&&word(0x22009+16*i)==0&&word(0x2200d+16*i)==1&&(ov&0xffff)==sequence[which]++);
  }assert(memory[0x22000]==0xa5&&memory[0x22001+16*count]==0xa5);
 }
 for(unsigned i=0;i<2;i++)assert(!pthread_join(threads[i],NULL)&&sequence[i]==64);
 assert(!dequeue(port,0)&&tribes_get_last_error()==258);
 assert(call(4,&other,1));assert(call(4,&port,1));
 /* Queue memory remains owned by the socket bindings after public port close. */
 assert(!tribes_socket_post_completion(base,e[0].token,1,7,0)&&tribes_get_last_error()==735);
 assert(!tribes_socket_post_completion(base,e[1].token,1,8,0)&&tribes_get_last_error()==735);
 for(unsigned i=0;i<2;i++){close_endpoint(e+i);assert(!associate(e[i].token,port,1)&&tribes_get_last_error()==6);assert(!modes(e[i].token,0)&&tribes_get_last_error()==6);}
 close_races();
 /* Owner detachment and final WSACleanup both release retained associations. */
 int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);uint32_t borrowed=tribes_socket_attach(base,fd,&cpu);assert(borrowed!=UINT32_MAX);port=make_port();assert(associate(borrowed,port,77)==port);
 assert(tribes_socket_detach(base,borrowed,&cpu)==fd&&!close(fd));assert(!tribes_socket_post_completion(base,borrowed,1,1,0)&&tribes_get_last_error()==6);assert(call(4,&port,1));
 for(unsigned i=0;i<128;i++){
  uint32_t a[]={2,2,17},s=call(9,a,3);port=make_port();assert(s!=UINT32_MAX&&port&&associate(s,port,i)==port);
  if(i&1){assert(call(4,&port,1));assert(!call(10,&s,1));}else{assert(!call(10,&s,1));assert(call(4,&port,1));}
 }
 uint32_t a[]={2,1,6},s=call(9,a,3);port=make_port();assert(associate(s,port,9)==port);assert(!call(8,NULL,0));assert(!tribes_socket_post_completion(base,s,1,1,0)&&tribes_get_last_error()==6);assert(call(4,&port,1));
 puts("PASS: real socket/queue association, retained keys, two TCP streams and128 concurrent completion deliveries, additive byte notification flags/immediate skip, foreign/stale/type checks, public-port close, socket close, owner detach and WSACleanup lifetime; emitted ABI/CPU/LastError and checked native ownership. No overlapped guest-I/O or authentication claim.");return 0;
}
