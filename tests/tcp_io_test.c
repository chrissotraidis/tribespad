#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/native_winsock.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
extern void tribes_connect_test_hold(void),tribes_connect_test_entered(void),tribes_connect_test_release(void);
static unsigned char *memory;static uint64_t base;static _cpu cpu;
static uint32_t word(uint32_t at){uint32_t v;memcpy(&v,memory+at,4);return v;}
static void put(uint32_t at,uint32_t v){memcpy(memory+at,&v,4);}
static uint32_t call(uint32_t pc,const uint32_t *args,unsigned count){
 uint32_t top=cpu._esp;cpu._esp-=count*4;if(count)memcpy(memory+cpu._esp,args,count*4);_cpu saved=cpu;
 tribes_enter(&cpu,pc);saved._eax=cpu._eax;saved._esp=top;
 assert(!memcmp(&saved,&cpu,sizeof cpu)&&(!count||!memcmp(memory+top-count*4,args,count*4)));return cpu._eax;
}
static void start(void){uint32_t a[]={0x101,0x20001};assert(!call(1,a,2));}
static uint32_t port(void){uint32_t a[]={UINT32_MAX,0,0,1};uint32_t p=call(8,a,4);assert(p);return p;}
static uint32_t client(uint32_t p,uint32_t key,int bound){
 uint32_t a[]={2,1,6},s=call(3,a,3);assert(s!=UINT32_MAX);
 put(0x21101,1);uint32_t nonblock[]={s,0x8004667e,0x21101};assert(!call(6,nonblock,3));
 if(bound){memset(memory+0x21201,0,16);memory[0x21201]=2;uint32_t bind[]={s,0x21201,16};assert(!call(5,bind,3));}
 uint32_t attach[]={s,p,key,0};assert(call(8,attach,4)==p);return s;
}
static int listener(struct sockaddr_in *a){
 int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);memset(a,0,sizeof *a);a->sin_len=sizeof *a;a->sin_family=AF_INET;a->sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 assert(!bind(fd,(void *)a,sizeof *a)&&!listen(fd,4));socklen_t n=sizeof *a;assert(!getsockname(fd,(void *)a,&n));return fd;
}
static void destination(const struct sockaddr_in *a){memset(memory+0x21301,0,16);memory[0x21301]=2;memcpy(memory+0x21303,&a->sin_port,2);memcpy(memory+0x21305,&a->sin_addr,4);}
static uint32_t extension(uint32_t s){
 static const unsigned char id[]={0xb9,7,0xa2,0x25,0xf3,0xdd,0x60,0x46,0x8e,0xe9,0x76,0xe5,0x8c,0x74,6,0x3e};memcpy(memory+0x21401,id,16);
 uint32_t a[]={s,0xc8000006,0x21401,16,0x21501,4,0x21601,0,0};put(0x21501,0xa5a5a5a5);put(0x21601,0xa5a5a5a5);tribes_set_last_error(919);
 assert(!call(7,a,9)&&word(0x21501)==TRIBES_CONNECT_EX_ENTRY&&word(0x21601)==4&&tribes_get_last_error()==919);
 memory[0x21401]^=1;assert(call(7,a,9)==UINT32_MAX&&tribes_get_last_error()==10045);assert(word(0x21501)==TRIBES_CONNECT_EX_ENTRY&&word(0x21601)==4);memory[0x21401]^=1;
 a[3]=15;assert(call(7,a,9)==UINT32_MAX&&tribes_get_last_error()==10014);a[3]=16;a[5]=3;assert(call(7,a,9)==UINT32_MAX&&tribes_get_last_error()==10014);
 return TRIBES_CONNECT_EX_ENTRY;
}
static uint32_t connect_call(uint32_t s,uint32_t pc){
 memset(memory+0x23000,0xa5,32);memset(memory+0x23001,0,20);put(0x23009,0x12345678);put(0x2300d,0x9abcdef0);put(0x21701,0xdeadbeef);
 uint32_t a[]={s,0x21301,16,0,123,0x21701,0x23001};uint32_t result=call(pc,a,7);assert(word(0x21701)==0xdeadbeef);return result;
}
static uint32_t dequeue(uint32_t p){uint32_t a[]={p,0x24001,1,0x25001,2000,0};tribes_set_last_error(919);assert(call(9,a,6)&&tribes_get_last_error()==919&&word(0x25001)==1);return word(0x24009);}
static void block(uint32_t status,uint32_t key){
 assert(word(0x24001)==key&&word(0x24005)==0x23001&&word(0x24009)==status&&word(0x2400d)==0);
 assert(word(0x23001)==status&&word(0x23005)==0&&word(0x23009)==0x12345678&&word(0x2300d)==0x9abcdef0&&word(0x23011)==0&&memory[0x23000]==0xa5&&memory[0x23015]==0xa5);
 assert(!tribes_socket_pending_connect_count());
}
static void close_pair(uint32_t s,uint32_t p){assert(!call(4,&s,1));assert(call(11,&p,1));}
static uint32_t io(uint32_t pc,uint32_t s,uint32_t rows,uint32_t count,uint32_t ov){uint32_t a[]={s,rows,count,0x27201,pc==14?0x27205:0,ov,0};return call(pc,a,7);}
static void prepare(uint32_t rows,uint32_t data,uint32_t length,uint32_t ov){put(rows,length);put(rows+4,data);put(0x27201,0xdeadbeef);put(0x27205,0);if(ov){memset(memory+ov,0,20);put(ov+8,0x12345678);put(ov+12,0x9abcdef0);}}
static void packet(uint32_t p,uint32_t key,uint32_t ov,uint32_t status,uint32_t bytes){
 uint32_t a[]={p,0x24001,1,0x25001,3000,0};assert(call(9,a,6)&&word(0x25001)==1);
 assert(word(0x24001)==key&&word(0x24005)==ov&&word(0x24009)==status&&word(0x2400d)==bytes);
 assert(word(ov)==status&&word(ov+4)==bytes&&word(ov+8)==0x12345678&&word(ov+12)==0x9abcdef0&&word(ov+16)==0);
}
static void empty(uint32_t p){uint32_t a[]={p,0x24001,1,0x25001,30,0};assert(!call(9,a,6)&&tribes_get_last_error()==258&&word(0x25001)==0);}
static int connected(uint32_t *s,uint32_t *p){
 struct sockaddr_in address;int server=listener(&address);destination(&address);*p=port();*s=client(*p,77,1);
 assert(!connect_call(*s,extension(*s))&&tribes_get_last_error()==997);assert(!dequeue(*p));block(0,77);
 struct pollfd r={server,POLLIN,0};assert(poll(&r,1,1000)==1);int peer=accept(server,NULL,NULL);assert(peer>=0&&!close(server));
 uint32_t update[]={*s,0xffff,0x7010,0,0};assert(!call(13,update,5));return peer;
}
static void idle(void){for(unsigned i=0;tribes_socket_pending_io_count();i++){assert(i<3000);usleep(1000);}}
static uint32_t cancel_ex(uint32_t s,uint32_t ov){uint32_t a[]={s,ov};return call(16,a,2);}
typedef struct {pthread_mutex_t lock;pthread_cond_t changed;uint32_t s;int submitted,action,finished,exit;} Issuer;
static void *other_issuer(void *opaque){
 Issuer *q=opaque;_cpu c=cpu;c._esp=0x1cff1;uint32_t a[]={q->s,0x27001,1,0x27201,0x27205,0x28001,0};c._esp-=sizeof a;memcpy(memory+c._esp,a,sizeof a);
 tribes_enter(&c,14);assert(c._eax==UINT32_MAX&&tribes_get_last_error()==997);
 assert(!pthread_mutex_lock(&q->lock));q->submitted=1;assert(!pthread_cond_broadcast(&q->changed));while(!q->action)assert(!pthread_cond_wait(&q->changed,&q->lock));int action=q->action;assert(!pthread_mutex_unlock(&q->lock));
 if(action==1){c._esp=0x1cfff-4;memcpy(memory+c._esp,&q->s,4);tribes_set_last_error(919);tribes_enter(&c,17);assert(c._eax==1&&tribes_get_last_error()==919);}
 assert(!pthread_mutex_lock(&q->lock));q->finished=1;assert(!pthread_cond_broadcast(&q->changed));while(!q->exit)assert(!pthread_cond_wait(&q->changed,&q->lock));assert(!pthread_mutex_unlock(&q->lock));return NULL;
}
static void explicit_cancellation(void){
 uint32_t s,p;int peer=connected(&s,&p);
 /* Exact-request miss and cancellation do not dereference OVERLAPPED, touch
    LastError on success, close the socket, consume bytes or duplicate packets. */
 prepare(0x27001,0,0,0x28001);assert(io(14,s,0x27001,1,0x28001)==UINT32_MAX&&tribes_get_last_error()==997);empty(p);
 assert(!cancel_ex(s,0xffffffff)&&tribes_get_last_error()==1168);assert(tribes_memory_permissions_set(0x28000,4096,0));tribes_set_last_error(919);
 assert(cancel_ex(s,0x28001)&&tribes_get_last_error()==919);assert(tribes_memory_permissions_set(0x28000,4096,3));packet(p,77,0x28001,0xc0000120,0);idle();empty(p);
 assert(!cancel_ex(s,0x28001)&&tribes_get_last_error()==1168);tribes_set_last_error(919);assert(call(17,&s,1)&&tribes_get_last_error()==919);
 int fd=tribes_socket_duplicate(base,s);struct sockaddr_in endpoint;socklen_t n=sizeof endpoint;assert(fd>=0&&!getpeername(fd,(void *)&endpoint,&n)&&!close(fd));
 assert(send(peer,"z",1,0)==1);prepare(0x27001,0,0,0x28001);assert(io(14,s,0x27001,1,0x28001)==UINT32_MAX&&tribes_get_last_error()==997);packet(p,77,0x28001,0,0);idle();prepare(0x27001,0x30001,8,0);assert(!io(14,s,0x27001,1,0)&&word(0x27201)==1&&memory[0x30001]=='z');close_pair(s,p);assert(!cancel_ex(s,0)&&tribes_get_last_error()==6);assert(!close(peer));
 /* CancelIo from the main thread leaves another live issuer's operation
    alone. Its owner cancels it; CancelIoEx from main can also select it. */
 for(unsigned mode=0;mode<2;mode++){
  peer=connected(&s,&p);prepare(0x27001,0,0,0x28001);Issuer q={.lock=PTHREAD_MUTEX_INITIALIZER,.changed=PTHREAD_COND_INITIALIZER,.s=s};pthread_t thread;assert(!pthread_create(&thread,NULL,other_issuer,&q));
  assert(!pthread_mutex_lock(&q.lock));while(!q.submitted)assert(!pthread_cond_wait(&q.changed,&q.lock));assert(!pthread_mutex_unlock(&q.lock));tribes_set_last_error(919);assert(call(17,&s,1)&&tribes_get_last_error()==919);empty(p);assert(word(0x28001)==0x103);
  if(mode)assert(cancel_ex(s,0x28001));
  assert(!pthread_mutex_lock(&q.lock));q.action=mode?2:1;assert(!pthread_cond_broadcast(&q.changed));while(!q.finished)assert(!pthread_cond_wait(&q.changed,&q.lock));assert(!pthread_mutex_unlock(&q.lock));
  packet(p,77,0x28001,0xc0000120,0);idle();empty(p);assert(!pthread_mutex_lock(&q.lock));q.exit=1;assert(!pthread_cond_broadcast(&q.changed));assert(!pthread_mutex_unlock(&q.lock));assert(!pthread_join(thread,NULL));assert(!pthread_cond_destroy(&q.changed)&&!pthread_mutex_destroy(&q.lock));close_pair(s,p);assert(!close(peer));
 }
 /* Real data-ready versus cancellation races always yield one packet and
    leave the actual incoming byte available for synchronous consumption. */
 peer=connected(&s,&p);
 for(unsigned i=0;i<32;i++){
  prepare(0x27001,0,0,0x28001);assert(io(14,s,0x27001,1,0x28001)==UINT32_MAX&&tribes_get_last_error()==997);assert(send(peer,"r",1,0)==1);uint32_t marked=cancel_ex(s,0x28001);if(!marked)assert(tribes_get_last_error()==1168);
  packet(p,77,0x28001,marked?0xc0000120:0,0);idle();empty(p);prepare(0x27001,0x30001,8,0);assert(!io(14,s,0x27001,1,0)&&word(0x27201)==1&&memory[0x30001]=='r');
 }
 close_pair(s,p);assert(!close(peer));
 /* Process-wide cancellation covers a read and both ordered writes without
    shutting down the connection. Actual partial-send counts are preserved. */
 peer=connected(&s,&p);fd=tribes_socket_duplicate(base,s);int small=4096;assert(fd>=0&&!setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&small,sizeof small)&&!close(fd));
 prepare(0x27001,0,0,0x28001);assert(io(14,s,0x27001,1,0x28001)==UINT32_MAX&&tribes_get_last_error()==997);
 prepare(0x27001,0x100000,4*1024*1024,0x29001);assert(io(15,s,0x27001,1,0x29001)==UINT32_MAX&&tribes_get_last_error()==997);
 prepare(0x27001,0x30001,6,0x29041);assert(io(15,s,0x27001,1,0x29041)==UINT32_MAX&&tribes_get_last_error()==997);assert(cancel_ex(s,0));
 unsigned found=0;for(unsigned i=0;i<3;i++){uint32_t a[]={p,0x24001,1,0x25001,3000,0};assert(call(9,a,6)&&word(0x24001)==77&&word(0x24009)==0xc0000120);uint32_t ov=word(0x24005);found|=ov==0x28001?1:ov==0x29001?2:ov==0x29041?4:0;assert(word(ov)==0xc0000120&&word(ov+4)==word(0x2400d));if(ov!=0x29001)assert(!word(ov+4));else assert(word(ov+4)<=4*1024*1024);}assert(found==7);idle();empty(p);
 fd=tribes_socket_duplicate(base,s);n=sizeof endpoint;assert(fd>=0&&!getpeername(fd,(void *)&endpoint,&n)&&!close(fd));assert(!cancel_ex(s,0)&&tribes_get_last_error()==1168);close_pair(s,p);assert(!close(peer));
}
#ifdef CHECKED
static void io_fault(uint32_t s,unsigned which){
 uint32_t page=which==1?0x28000:which==2?0x30000:0x27000;prepare(0x27001,0x30001,which==2?1:0,0x28001);memory[0x30001]='x';
 assert(tribes_memory_permissions_set(page,4096,which==1?1:0));pid_t child=fork();assert(child>=0);
 if(!child){(void)io(which==2?15:14,s,0x27001,1,0x28001);_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(tribes_memory_permissions_set(page,4096,3));
}
#endif
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x30000,PROT_READ|PROT_WRITE)&&!mprotect(memory+0x100000,0x500000,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));
#ifdef CHECKED
 tribes_memory_external_enable();
#endif
 memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x1fff1;start();uint32_t s,p;int peer=connected(&s,&p);
#ifdef CHECKED
 for(unsigned i=0;i<3;i++)io_fault(s,i);
#endif
 /* Actual incoming bytes produce a zero-byte readiness packet and remain
    available for the original adjacent synchronous scatter receive. */
 prepare(0x27001,UINT32_MAX,0,0x28001);assert(io(14,s,0x27001,1,0x28001)==UINT32_MAX&&tribes_get_last_error()==997&&word(0x28001)==0x103);empty(p);
 memset(memory+0x27001,0xa5,8);assert(send(peer,"abc",3,0)==3);packet(p,77,0x28001,0,0);idle();assert(word(0x27201)==0xdeadbeef&&word(0x27205)==0);
 memset(memory+0x30000,0xa5,32);prepare(0x27001,0x30001,1,0);put(0x27009,0);put(0x2700d,UINT32_MAX);put(0x27011,4);put(0x27015,0x30009);
 tribes_set_last_error(919);assert(!io(14,s,0x27001,3,0)&&word(0x27201)==3&&tribes_get_last_error()==919);assert(memory[0x30001]=='a'&&!memcmp(memory+0x30009,"bc",2)&&memory[0x30000]==0xa5&&memory[0x3000b]==0xa5);
 prepare(0x27001,0x30001,8,0);assert(io(14,s,0x27001,1,0)==UINT32_MAX&&tribes_get_last_error()==10035&&word(0x27201)==0xdeadbeef&&word(0x27205)==0);
 /* Two queued writes under real kernel backpressure retain FIFO payload and
    completion order. Staging never exposes guest payloads to native send. */
 int fd=tribes_socket_duplicate(base,s),small=4096;assert(fd>=0&&!setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&small,sizeof small)&&!close(fd));
 const uint32_t large=1024*1024;for(uint32_t i=0;i<large;i++)memory[0x100000+i]=(unsigned char)(i*13+7);
 prepare(0x27001,0x100000,large/2,0x29001);put(0x27009,large/2);put(0x2700d,0x100000+large/2);
 assert(io(15,s,0x27001,2,0x29001)==UINT32_MAX&&tribes_get_last_error()==997&&word(0x27201)==0xdeadbeef);
 memcpy(memory+0x30001,"second",6);prepare(0x27001,0x30001,6,0x29041);assert(io(15,s,0x27001,1,0x29041)==UINT32_MAX&&tribes_get_last_error()==997);
 unsigned char *received=memory+0x400000;uint32_t got=0;while(got<large+6){struct pollfd r={peer,POLLIN,0};assert(poll(&r,1,3000)==1);ssize_t n=recv(peer,received+got,large+6-got,0);assert(n>0);got+=(uint32_t)n;}
 assert(!memcmp(received,memory+0x100000,large)&&!memcmp(received+large,"second",6));packet(p,77,0x29001,0,large);packet(p,77,0x29041,0,6);idle();empty(p);
 /* Synchronous write uses actual kernel byte count, and flags fail without
    touching output or submitting completion. */
 prepare(0x27001,0x30001,6,0);tribes_set_last_error(919);assert(!io(15,s,0x27001,1,0)&&word(0x27201)==6&&tribes_get_last_error()==919);assert(recv(peer,received,6,MSG_WAITALL)==6&&!memcmp(received,"second",6));
 uint32_t bad[]={s,0x27001,1,0x27201,1,0,0};put(0x27201,0xdeadbeef);assert(call(15,bad,7)==UINT32_MAX&&tribes_get_last_error()==10045&&word(0x27201)==0xdeadbeef);
 /* Native orderly EOF is a real readiness completion followed by zero read. */
 prepare(0x27001,0,0,0x28001);assert(io(14,s,0x27001,1,0x28001)==UINT32_MAX&&tribes_get_last_error()==997);assert(!close(peer));packet(p,77,0x28001,0,0);idle();prepare(0x27001,0x30001,8,0);assert(!io(14,s,0x27001,1,0)&&word(0x27201)==0);close_pair(s,p);
 /* Retire pending receive and backpressured sends through real cancellation
    on socket close or final Winsock cleanup; port-first close retains owners. */
 for(unsigned mode=0;mode<3;mode++){
  peer=connected(&s,&p);prepare(0x27001,0,0,0x28001);assert(io(14,s,0x27001,1,0x28001)==UINT32_MAX&&tribes_get_last_error()==997);empty(p);
  fd=tribes_socket_duplicate(base,s);assert(fd>=0&&!setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&small,sizeof small)&&!close(fd));
  prepare(0x27001,0x100000,4*large,0x29001);assert(io(15,s,0x27001,1,0x29001)==UINT32_MAX&&tribes_get_last_error()==997);
  prepare(0x27001,0x30001,6,0x29041);assert(io(15,s,0x27001,1,0x29041)==UINT32_MAX&&tribes_get_last_error()==997);
  if(mode==2)assert(call(11,&p,1));if(mode==1)assert(!call(2,NULL,0));else assert(!call(4,&s,1));
  if(mode<2){unsigned found=0;for(unsigned i=0;i<3;i++){uint32_t a[]={p,0x24001,1,0x25001,3000,0};assert(call(9,a,6)&&word(0x24001)==77&&word(0x24009)==0xc0000120);uint32_t ov=word(0x24005);found|=ov==0x28001?1:ov==0x29001?2:ov==0x29041?4:0;assert(word(ov)==0xc0000120);}assert(found==7);assert(call(11,&p,1));}
  idle();assert(!close(peer));if(mode==1)start();
 }
 explicit_cancellation();assert(!call(2,NULL,0));puts("PASS: original zero-read readiness, preserved bytes/scatter receive, would-block/EOF, ordered native backpressure writes and close/cleanup/port lifetimes and exact/process-wide/thread-scoped cancellation without closing sockets through emitted checked ABI");return 0;
}
