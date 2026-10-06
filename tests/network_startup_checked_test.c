#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/core/native_winsock.h"
#include <fcntl.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t),network_bad_abi(unsigned);
extern int network_guest_pointer(unsigned,int,void *);
static unsigned char *memory;
static uint32_t word(uint32_t address){uint32_t value;memcpy(&value,memory+address,4);return value;}
static void put(uint32_t address,uint32_t value){memcpy(memory+address,&value,4);}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args){
 uint32_t top=cpu->_esp;cpu->_esp-=count*4;if(count)memcpy(memory+cpu->_esp,args,count*4);_cpu saved=*cpu;
 tribes_enter(cpu,target);saved._eax=cpu->_eax;saved._esp=top;
 assert(!memcmp(cpu,&saved,sizeof saved));if(count)assert(!memcmp(memory+top-count*4,args,count*4));return cpu->_eax;
}
static void child_stop(unsigned kind,int fd,int bad_abi){
 memset(memory+0x30000,0xa7,4096);pid_t child=fork();assert(child>=0);
 if(!child){if(bad_abi)network_bad_abi(kind);else (void)network_guest_pointer(kind,fd,memory+0x30000);_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 for(unsigned i=0;i<4096;i++)assert(memory[0x30000+i]==0xa7);
}
static void protocol(_cpu *cpu,uint32_t s,unsigned type){
 unsigned char provider[16]={0};
 for(unsigned wide=0;wide<2;wide++){
  uint32_t required=wide?628:372,record=0x28001,length=0x27001;
  for(unsigned short_case=0;short_case<3;short_case++){
   memset(memory+0x28000,0xa7,1024);put(length,short_case==0?0:short_case==1?required-1:UINT32_MAX);
   uint32_t args[]={s,0xffff,wide?0x2005:0x2004,record,length};assert(invoke(cpu,7,5,args)==UINT32_MAX&&tribes_get_last_error()==10014&&word(length)==required);
   for(unsigned i=0;i<1024;i++)assert(memory[0x28000+i]==0xa7);
  }
  put(length,required);uint32_t query[]={s,0xffff,wide?0x2005:0x2004,0,length};assert(invoke(cpu,7,5,query)==UINT32_MAX&&word(length)==required&&tribes_get_last_error()==10014);
  put(length,1024);query[3]=record;tribes_set_last_error(12345);assert(!invoke(cpu,7,5,query)&&tribes_get_last_error()==12345&&word(length)==required);
  assert(memory[record-1]==0xa7&&memory[record+required]==0xa7);
  assert(word(record)==(type==1?0x26u:9u)&&!(word(record)&0x20000));
  assert(!word(record+4)&&!word(record+8)&&!word(record+12)&&word(record+16)==8);
  if(!wide)memcpy(provider,memory+record+20,16);else assert(!memcmp(provider,memory+record+20,16));
  assert(memcmp(provider,(unsigned char[16]){0},16));
  assert(word(record+36)==type&&word(record+40)==1&&word(record+72)==2&&word(record+76)==2&&word(record+80)==16&&word(record+84)==16);
  for(unsigned offset=44;offset<72;offset+=4)assert(!word(record+offset));
  assert(word(record+88)==type&&word(record+92)==(type==1?6u:17u)&&!word(record+96)&&!word(record+100)&&!word(record+104)&&word(record+108)==(type==1?0u:65507u)&&!word(record+112));
  const char *name=type==1?"TribesPad native TCP/IPv4":"TribesPad native UDP/IPv4";
  for(unsigned i=0;i<256;i++){uint16_t unit=0;if(wide)memcpy(&unit,memory+record+116+2*i,2);else unit=memory[record+116+i];assert(unit==(i<strlen(name)?(unsigned char)name[i]:0));}
 }
}
static void protocol_fault(_cpu *cpu,uint32_t s,int length_fault){
 memset(memory+0x28000,0xa7,4096);put(0x27001,628);uint32_t page=length_fault?0x27000:0x28000;
 unsigned char saved[4096];memcpy(saved,memory+page,4096);assert(tribes_memory_permissions_set(page,4096,1));
 pid_t child=fork();assert(child>=0);if(!child){uint32_t args[]={s,0xffff,0x2005,0x28001,0x27001};invoke(cpu,7,5,args);_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 assert(!memcmp(saved,memory+page,4096));assert(tribes_memory_permissions_set(page,4096,3));
}
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);uint64_t base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x40000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x1ffe1;tribes_set_last_error(12345);
 uint32_t invalid[]={0,0xffff,0x2005,0,0};assert(invoke(&cpu,7,5,invalid)==UINT32_MAX&&tribes_get_last_error()==10093);tribes_set_last_error(12345);
 uint32_t startup[]={0x101,0x20001};assert(!invoke(&cpu,1,2,startup));uint16_t version;memcpy(&version,memory+0x20001,2);assert(version==0x101&&tribes_get_last_error()==12345);
 /* Inspect the real owner descriptor: changes must not land on a duplicate,
    change O_NONBLOCK, or alter bits outside the mask. */
 int original=socket(AF_INET,SOCK_STREAM,0);assert(original>=0);
 assert(!fcntl(original,F_SETFD,FD_CLOEXEC));assert(!fcntl(original,F_SETFL,O_NONBLOCK));
 uint32_t attached=tribes_socket_attach(base,original,&cpu);assert(attached!=UINT32_MAX);
 const uint32_t masks[]={1,0,1,1,1,0},flags[]={1,0,0,0,3,1};
 const int expected[]={0,0,FD_CLOEXEC,FD_CLOEXEC,0,0};
 for(unsigned i=0;i<6;i++){
  uint32_t info[]={attached,masks[i],flags[i]};tribes_set_last_error(12345);
  assert(invoke(&cpu,8,3,info)==1&&tribes_get_last_error()==12345);
  assert(fcntl(original,F_GETFD)==expected[i]&&(fcntl(original,F_GETFL)&O_NONBLOCK));
 }
 assert(!tribes_set_socket_handle_information(base+1,attached,1,0)&&tribes_get_last_error()==6);
 for(unsigned i=0;i<2;i++){
  pid_t child=fork();assert(child>=0);
  if(!child){uint32_t unsupported[]={i?0xe1000001u:attached,i?1u:2u,0};invoke(&cpu,8,3,unsupported);_exit(90);}
  int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  assert(fcntl(original,F_GETFD)==0&&(fcntl(original,F_GETFL)&O_NONBLOCK));
 }
 assert(tribes_socket_detach(base,attached,&cpu)==original);assert(!close(original));
 uint32_t stale[]={attached,1,0};assert(!invoke(&cpu,8,3,stale)&&tribes_get_last_error()==6);
 tribes_set_last_error(12345);
 for(unsigned i=0;i<32;i++){
  uint32_t type=i&1?1:2,args[]={2,type,0},s=invoke(&cpu,3,3,args);assert(s!=UINT32_MAX);uint32_t info[]={s,1,0};assert(invoke(&cpu,8,3,info)==1&&tribes_get_last_error()==12345);protocol(&cpu,s,type);
  if(!i){protocol_fault(&cpu,s,0);protocol_fault(&cpu,s,1);}
  unsigned char addr[16]={2,0,0,0,127,0,0,1};memcpy(memory+0x21001,addr,16);
  uint32_t b[]={s,0x21001,16};assert(!invoke(&cpu,5,3,b));uint32_t length=16;memcpy(memory+0x22001,&length,4);memset(memory+0x23000,0xa7,18);
  uint32_t n[]={s,0x23001,0x22001};assert(!invoke(&cpu,6,3,n));memcpy(&length,memory+0x22001,4);assert(length==16&&memory[0x23000]==0xa7&&memory[0x23011]==0xa7);
  assert(memory[0x23001]==2&&!memory[0x23002]&&!memcmp(memory+0x23005,addr+4,4));uint16_t port;memcpy(&port,memory+0x23003,2);assert(port);
  int peer=socket(AF_INET,type==1?SOCK_STREAM:SOCK_DGRAM,0);assert(peer>=0);struct sockaddr_in local={0};local.sin_len=sizeof local;local.sin_family=AF_INET;local.sin_port=port;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  assert(bind(peer,(void *)&local,sizeof local)==-1&&errno==EADDRINUSE);assert(!invoke(&cpu,4,1,&s));assert(!bind(peer,(void *)&local,sizeof local));assert(!close(peer));
  assert(tribes_get_last_error()==12345);put(0x27001,999);uint32_t closed[]={s,0xffff,0x2005,0x28001,0x27001};assert(invoke(&cpu,7,5,closed)==UINT32_MAX&&tribes_get_last_error()==10038&&word(0x27001)==999);tribes_set_last_error(12345);
 }
 assert(!invoke(&cpu,2,0,NULL));int fd=socket(AF_INET,SOCK_DGRAM,0);assert(fd>=0);
 for(unsigned i=0;i<5;i++)child_stop(i,fd,0);
 for(unsigned i=0;i<6;i++)child_stop(i,fd,1);
 assert(!close(fd));puts("PASS: actual socket inheritance flags and masked no-op preserve nonblocking state, stale identity fails; guarded Winsock startup, 32 emitted-ABI TCP/UDP socket/bind/name/close cycles with real kernel port ownership/release, ANSI/UTF16 protocol records, explicit non-IFS capabilities, short-buffer size negotiation, closed/uninitialized handle errors, two protected-output faults, CPU/stack/LastError preservation, five guest-pointer rejections and six wrong-ABI pre-call stops");return 0;
}
