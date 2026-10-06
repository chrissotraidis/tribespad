#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/native_winsock.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
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
#ifdef CHECKED
static void fault(uint32_t s,uint32_t p,unsigned which){
 memset(memory+0x23001,0,20);uint32_t page=which==1?0x23000:0x21000;unsigned char saved[4096];memcpy(saved,memory+page,4096);
 assert(tribes_memory_permissions_set(page,4096,which==0?0:1));pid_t child=fork();assert(child>=0);
 if(!child){if(which<2){uint32_t a[]={s,0x21301,16,0,0,0,0x23001};(void)call(TRIBES_CONNECT_EX_ENTRY,a,7);}else{uint32_t a[]={s,0xc8000006,0x21401,16,0x21501,4,0x21601,0,0};(void)call(7,a,9);}_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(saved,memory+page,4096));assert(tribes_memory_permissions_set(page,4096,3));(void)p;
}
#endif
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x20000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));
#ifdef CHECKED
 tribes_memory_external_enable();
#endif
 memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x1fff1;start();
 struct sockaddr_in address;int server=listener(&address);destination(&address);uint32_t p=port(),s=client(p,77,1),entry=extension(s);
 uint32_t update[]={s,0xffff,0x7010,0,0};assert(call(13,update,5)==UINT32_MAX&&tribes_get_last_error()==10057);
#ifdef CHECKED
 for(unsigned i=0;i<3;i++)fault(s,p,i);
#endif
 assert(!connect_call(s,entry)&&tribes_get_last_error()==997&&word(0x23001)==0x103);
 assert(!dequeue(p));block(0,77);tribes_set_last_error(919);assert(!call(13,update,5)&&tribes_get_last_error()==919);
 struct pollfd ready={server,POLLIN,0};assert(poll(&ready,1,1000)==1);int peer=accept(server,NULL,NULL),fd=tribes_socket_duplicate(base,s);assert(peer>=0&&fd>=0);
 char sent='x',received=0;assert(send(peer,&sent,1,0)==1);ready=(struct pollfd){fd,POLLIN,0};assert(poll(&ready,1,1000)==1&&recv(fd,&received,1,0)==1&&received==sent);assert(!close(fd));
 assert(!connect_call(s,entry)&&tribes_get_last_error()==10056);close_pair(s,p);assert(recv(peer,&received,1,0)==0&&!close(peer)&&!close(server));
 /* An unbound owned socket fails before native initiation. */
 p=port();s=client(p,78,0);assert(!connect_call(s,entry)&&tribes_get_last_error()==10022);close_pair(s,p);
 /* Actual kernel refusal through both dequeue APIs; no fabricated success. */
 server=listener(&address);assert(!close(server));destination(&address);
 for(unsigned i=0;i<2;i++){
  p=port();s=client(p,90+i,1);uint32_t mode[]={s,3};assert(call(12,mode,2));assert(!connect_call(s,entry)&&tribes_get_last_error()==997&&word(0x23001)==0x103);
  if(!i){assert(dequeue(p)==0xc0000236);block(0xc0000236,90);}
  else{uint32_t a[]={p,0x26001,0x26005,0x26009,2000};assert(!call(10,a,5)&&tribes_get_last_error()==1225);assert(word(0x26001)==0&&word(0x26005)==91&&word(0x26009)==0x23001&&word(0x23001)==0xc0000236&&!tribes_socket_pending_connect_count());}
  close_pair(s,p);
 }
 /* A real started native connect is held before completion commit, then
    canceled by socket close or final WSACleanup. Exactly one packet results. */
 for(unsigned i=0;i<3;i++){
  server=listener(&address);destination(&address);p=port();s=client(p,100+i,1);tribes_connect_test_hold();
  assert(!connect_call(s,entry)&&tribes_get_last_error()==997);tribes_connect_test_entered();assert(word(0x23001)==0x103&&tribes_socket_pending_connect_count()==1);
  uint32_t again[]={s,0x21301,16,0,0,0,0x23001};assert(!call(entry,again,7)&&tribes_get_last_error()==10037);
  if(i==1)assert(!call(2,NULL,0));else assert(!call(4,&s,1));
  if(i==2)assert(call(11,&p,1));tribes_connect_test_release();
  if(i<2){assert(dequeue(p)==0xc0000120);block(0xc0000120,100+i);assert(call(11,&p,1));}
  else{for(unsigned wait=0;tribes_socket_pending_connect_count();wait++){assert(wait<2000);usleep(1000);}assert(word(0x23001)==0x103);}
  assert(!close(server));if(i==1)start();
 }
 /* Explicit cancellation selects the actual pending ConnectEx identity. */
 server=listener(&address);destination(&address);p=port();s=client(p,150,1);tribes_connect_test_hold();assert(!connect_call(s,entry)&&tribes_get_last_error()==997);tribes_connect_test_entered();
 uint32_t cancel[]={s,0x23001};tribes_set_last_error(919);assert(call(16,cancel,2)&&tribes_get_last_error()==919&&word(0x23001)==0x103);tribes_connect_test_release();assert(dequeue(p)==0xc0000120);block(0xc0000120,150);assert(!call(16,cancel,2)&&tribes_get_last_error()==1168);close_pair(s,p);assert(!close(server));
 /* Ungated connect/close races retain the descriptor and queue to completion. */
 for(unsigned i=0;i<32;i++){
  server=listener(&address);destination(&address);p=port();s=client(p,200+i,1);assert(!connect_call(s,entry)&&tribes_get_last_error()==997);assert(!call(4,&s,1));
  uint32_t status=dequeue(p);assert(!status||status==0xc0000120);block(status,200+i);assert(call(11,&p,1)&&!close(server));
 }
 assert(!call(2,NULL,0));puts("PASS: finite ConnectEx discovery/emitted ABI, actual asynchronous native TCP and bytes, context commit, real refusal statuses through both dequeues, pending notification flags, socket/port/WSACleanup cancellation and32 close races; checked ownership/status-block lifetime. No authentication claim.");return 0;
}
