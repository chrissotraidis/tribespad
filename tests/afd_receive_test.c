#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/native_winsock.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <arpa/inet.h>
#include <assert.h>
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
extern void afd_bad_datagram_pointer(void *,unsigned);
static unsigned char *memory;static uint64_t base;static _cpu cpu;
static void put(uint32_t a,uint32_t v){memcpy(memory+a,&v,4);}
static uint32_t word(uint32_t a){uint32_t v;memcpy(&v,memory+a,4);return v;}
static uint32_t call(uint32_t pc,const uint32_t *args,unsigned count){
 uint32_t top=cpu._esp;cpu._esp-=count*4;if(count)memcpy(memory+cpu._esp,args,count*4);_cpu saved=cpu;
 tribes_enter(&cpu,pc);saved._eax=cpu._eax;saved._esp=top;
 assert(!memcmp(&saved,&cpu,sizeof cpu)&&(!count||!memcmp(memory+top-count*4,args,count*4)));return cpu._eax;
}
static void idle(void){for(unsigned i=0;i<3000&&tribes_socket_pending_io_count();i++)usleep(1000);assert(!tribes_socket_pending_io_count());}
static void start(void){uint32_t a[]={0x202,0x20000};assert(!call(1,a,2));}
static uint32_t dequeue(uint32_t port,uint32_t timeout){uint32_t a[]={port,0x22001,1,0x23001,timeout,0};return call(9,a,6);}
static uint32_t receive(uint32_t s){uint32_t a[]={s,0,0,0x24ff8,0x24ff8,0x12017,0x21001,16,0,0};return call(18,a,10);}
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x30000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));
#ifdef CHECKED
 tribes_memory_external_enable();
 for(unsigned fault=0;fault<4;fault++){pid_t child=fork();assert(child>=0);if(!child){afd_bad_datagram_pointer(memory+0x20000,fault);_exit(99);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);}
#endif
 cpu._pointer_offset=base;cpu._esp=0x3f000;cpu.stack_bottom=memory+0x30000;cpu.stack_top=memory+0x40000;
 start();
 for(unsigned family=0;family<2;family++)for(unsigned mode=0;mode<5;mode++){
  uint32_t args[]={family?23:2,2,0};uint32_t s=call(3,args,3);assert(s!=UINT32_MAX);
  put(0x21101,1);uint32_t nonblock[]={s,0x8004667e,0x21101};assert(!call(6,nonblock,3));
  memset(memory+0x21201,0,28);memory[0x21201]=family?23:2;uint32_t b[]={s,0x21201,family?28:16};assert(!call(5,b,3));
  uint32_t create[]={UINT32_MAX,0,0,1},port=call(8,create,4);assert(port);uint32_t attach[]={s,port,0x98765432,0};assert(call(8,attach,4)==port);
  uint32_t request[]={0x21ff8,1,2,mode&1?0x20:0xa0};memcpy(memory+0x21001,request,16);put(0x21ff8,0);put(0x21ffc,0xffffffff);
  memset(memory+0x24ff0,0xa7,16);put(0x24ff8,0);put(0x24ffc,0);assert(tribes_memory_permissions_set(0x25000,4096,0));
  if(!family&&!mode){
#ifdef CHECKED
   for(unsigned fault=0;fault<3;fault++){
    pid_t child=fork();assert(child>=0);
    if(!child){if(fault==0){assert(tribes_memory_permissions_set(0x21000,4096,0));}else if(fault==1){put(0x21001,0x24ffd);}else assert(tribes_memory_permissions_set(0x24000,4096,1));receive(s);_exit(99);}
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
   }
#endif
  }
  tribes_set_last_error(919);assert(receive(s)==0x103&&tribes_get_last_error()==919);assert(word(0x24ff8)==0x103&&word(0x24ffc)==0&&memory[0x24ff7]==0xa7);
  assert(!dequeue(port,10)&&tribes_get_last_error()==258);
  /* The pending call must capture its request; its stack WSABUF is disposable. */
  memset(memory+0x21001,0xcc,16);memset(memory+0x21ff8,0xcc,8);
  if(!mode){
   int fd=tribes_socket_duplicate(base,s);assert(fd>=0);struct sockaddr_storage dest={0};socklen_t n=sizeof dest;assert(!getsockname(fd,(void *)&dest,&n));
   int peer=socket(family?AF_INET6:AF_INET,SOCK_DGRAM,0);assert(peer>=0);
   if(family)((struct sockaddr_in6 *)&dest)->sin6_addr=in6addr_loopback;else ((struct sockaddr_in *)&dest)->sin_addr.s_addr=htonl(INADDR_LOOPBACK);
   assert(sendto(peer,"ready",5,0,(void *)&dest,n)==5);
   assert(dequeue(port,3000)&&word(0x22001)==0x98765432&&word(0x22005)==0x24ff8&&word(0x22009)==0&&word(0x2200d)==0);
   idle();char data[8];assert(recv(fd,data,sizeof data,MSG_PEEK)==5&&!memcmp(data,"ready",5));
   uint32_t bufs[]={2,0x28101,3,0x28201};memcpy(memory+0x21301,bufs,16);put(0x21401,0);put(0x21405,0xdeadbeef);put(0x21409,28);memset(memory+0x26000,0xa7,32);
   uint32_t read[]={s,0x21301,2,0x21405,0x21401,0x26001,0x21409,0,0};
   tribes_set_last_error(919);assert(!call(19,read,9)&&tribes_get_last_error()==919&&word(0x21405)==5&&word(0x21409)==(family?28:16));
   assert(!memcmp(memory+0x28101,"re",2)&&!memcmp(memory+0x28201,"ady",3)&&memory[0x26000]==0xa7&&memory[0x26001+(family?28:16)]==0xa7);
   assert(call(19,read,9)==UINT32_MAX&&tribes_get_last_error()==10035);
   for(unsigned zero=0;zero<2;zero++){
    if(zero){put(0x21301,0);put(0x21305,0xffffffff);}
    memset(memory+0x29001,0,20);put(0x21405,0xdeadbeef);
    uint32_t write[]={s,0x21301,zero?1:2,0x21405,0,0x26001,word(0x21409),0x29001,0};
    assert(call(20,write,9)==UINT32_MAX&&tribes_get_last_error()==997&&word(0x21405)==0xdeadbeef);
    assert(dequeue(port,3000)&&word(0x22005)==0x29001&&word(0x22009)==0&&word(0x2200d)==(zero?0:5));idle();
    assert(recv(peer,data,sizeof data,0)==(zero?0:5));if(!zero)assert(!memcmp(data,"ready",5));
   }
   /* Truncation consumes exactly one datagram and reports the copied prefix. */
   assert(sendto(peer,"truncate",8,0,(void *)&dest,n)==8);struct pollfd readable={fd,POLLIN,0};assert(poll(&readable,1,1000)==1);put(0x21301,2);put(0x21305,0x28101);read[2]=1;
   uint32_t truncated=call(19,read,9);assert(truncated==UINT32_MAX&&tribes_get_last_error()==10040&&word(0x21405)==2&&!memcmp(memory+0x28101,"tr",2));
   assert(call(19,read,9)==UINT32_MAX&&tribes_get_last_error()==10035);
   assert(!close(peer)&&!close(fd));
  }else{
   if(mode==1){uint32_t cancel[]={s,0x24ff8};assert(call(16,cancel,2));}
   else if(mode==2)assert(!call(4,&s,1));
   else if(mode==3)assert(!call(2,NULL,0));
   else {assert(call(11,&port,1));assert(!call(4,&s,1));}
   if(mode<4){assert(dequeue(port,3000)&&word(0x22005)==0x24ff8&&word(0x22009)==0xc0000120&&word(0x2200d)==0);}
   idle();
  }
  if(mode<4){assert(!dequeue(port,0));assert(call(11,&port,1));}
  if(mode<2)assert(!call(4,&s,1));
  assert(tribes_memory_permissions_set(0x25000,4096,3));
  if(mode==3)start();
 }
 assert(!call(2,NULL,0));puts("PASS: original AFD zero-byte UDP readiness for IPv4/IPv6, retained datagram, real scatter UDP receive, atomic/zero-datagram IOCP send and truncation, emitted ten-argument NT ABI/LastError, exact eight-byte status output, discarded stack request and cancellation/close/cleanup/port lifetime");return 0;
}
