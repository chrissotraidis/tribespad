#include "../port/core/native_winsock.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t);
#ifdef CHECKED
extern void dns_host_pointer(unsigned,void *),dns_bad_result(void),dns_bad_free(void);
#endif
static unsigned char *memory;static _cpu cpu;
static uint32_t word(uint32_t p){uint32_t v;memcpy(&v,memory+p,4);return v;}
static void put(uint32_t p,uint32_t v){memcpy(memory+p,&v,4);}
static uint32_t call(unsigned id,const uint32_t *a,unsigned n){
 cpu._esp=0x1ffe1;memcpy(memory+cpu._esp,a,4*n);_cpu before=cpu;
 tribes_enter(&cpu,id);if(id==4)assert(cpu._eax==before._eax);
 before._eax=cpu._eax;before._esp=0x1ffe1+4*n;assert(!memcmp(&before,&cpu,sizeof cpu));
 assert(!memcmp(memory+0x1ffe1,a,4*n));return cpu._eax;
}
static void wide(uint32_t p,const char *s){do{uint16_t c=(unsigned char)*s;memcpy(memory+p,&c,2);p+=2;}while(*s++);}
static uint32_t query(const char *node,const char *service,uint32_t flags,uint32_t family,const char *fault){
 wide(0x11001,node);wide(0x12001,service);uint32_t h[8]={flags,family,1,6,0,0,0,0};memcpy(memory+0x13001,h,32);
 memset(memory+0x14000,0xa7,8);uint32_t a[]={0x11001,0x12001,0x13001,0x14001};
 if(fault){
  if(!strcmp(fault,"output"))assert(tribes_memory_permissions_set(0x14000,4096,1));
  if(!strcmp(fault,"node"))assert(tribes_memory_permissions_set(0x11000,4096,0));
  if(!strcmp(fault,"service"))assert(tribes_memory_permissions_set(0x12000,4096,0));
  if(!strcmp(fault,"hints"))assert(tribes_memory_permissions_set(0x13000,4096,0));
  if(!strcmp(fault,"cross-node")){a[0]=0x11fff;memory[0x11fff]='x';assert(tribes_memory_permissions_set(0x12000,4096,0));}
 }
 uint32_t status=call(3,a,4);assert(memory[0x14000]==0xa7&&memory[0x14005]==0xa7&&memory[0x14006]==0xa7);
 assert(tribes_get_last_error()==status);return status;
}
static void compare(const char *node,uint32_t family,uint32_t flags){
 struct addrinfo h={0},*native=NULL;h.ai_family=family==23?AF_INET6:family==2?AF_INET:AF_UNSPEC;h.ai_socktype=SOCK_STREAM;h.ai_protocol=IPPROTO_TCP;
 if(flags&2)h.ai_flags|=AI_CANONNAME;if(flags&4)h.ai_flags|=AI_NUMERICHOST;if(flags&8)h.ai_flags|=AI_NUMERICSERV;if(flags&0x400)h.ai_flags|=AI_ADDRCONFIG;
 assert(!getaddrinfo(node,"60659",&h,&native));assert(!query(node,"60659",flags,family,NULL));uint32_t head=word(0x14001),p=head;assert(head);
 unsigned count=0;for(struct addrinfo *a=native;a;a=a->ai_next){
  uint32_t native_flags=(a->ai_flags&AI_CANONNAME?2u:0)|(a->ai_flags&AI_NUMERICHOST?4u:0)|(a->ai_flags&AI_NUMERICSERV?8u:0)|(a->ai_flags&AI_ADDRCONFIG?0x400u:0);
  assert(p&&word(p)==native_flags&&word(p+4)==(a->ai_family==AF_INET?2u:23u));
  assert(word(p+8)==(uint32_t)a->ai_socktype&&word(p+12)==(uint32_t)a->ai_protocol);
  uint32_t address=word(p+24),name=word(p+20);uint16_t f;memcpy(&f,memory+address,2);assert(f==word(p+4));
  if(a->ai_family==AF_INET){struct sockaddr_in *s=(void *)a->ai_addr;assert(word(p+16)==16);assert(!memcmp(memory+address+2,&s->sin_port,2)&&!memcmp(memory+address+4,&s->sin_addr,4));}
  else{struct sockaddr_in6 *s=(void *)a->ai_addr;assert(word(p+16)==28);assert(!memcmp(memory+address+2,&s->sin6_port,2)&&!memcmp(memory+address+4,&s->sin6_flowinfo,4)&&!memcmp(memory+address+8,&s->sin6_addr,16)&&word(address+24)==s->sin6_scope_id);}
  assert(!!name==!!a->ai_canonname);if(name){assert(!(name&3));for(unsigned j=0;j<=strlen(a->ai_canonname);j++){uint16_t c;memcpy(&c,memory+name+2*j,2);assert(c==(unsigned char)a->ai_canonname[j]);}}
  p=word(p+28);assert(++count<4096);
 }assert(!p&&count);freeaddrinfo(native);
 /* Free must use private ownership even when guest public links are changed. */
 put(head+28,head);tribes_set_last_error(0x19a5);call(4,&head,1);assert(tribes_get_last_error()==0x19a5&&!tribes_global_size((uintptr_t)memory,head));
}
static void *parallel(void *unused){(void)unused;uint64_t base=(uintptr_t)memory;
 for(unsigned i=0;i<8;i++){assert(!tribes_get_addr_info_w(base,0x11001,0x12001,0x13001,0x15000+16*i));uint32_t head=word(0x15000+16*i);assert(head);tribes_free_addr_info_w(base,head);}return NULL;
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);uint64_t base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x10000,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));
#ifdef CHECKED
 tribes_memory_external_enable();
#endif
 memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;
 uint32_t startup[]={0x101,0x18001},empty[]={0,0,0,0x14001};assert(call(3,empty,4)==10093&&!word(0x14001));assert(!call(1,startup,2));
 const char *fault=argc==2?argv[1]:NULL;
#ifdef CHECKED
 if(fault&&!strncmp(fault,"external-",9)){dns_host_pointer((unsigned)(fault[9]-'0'),memory+0x11001);assert(0);}
 if(fault&&!strcmp(fault,"bad-result")){dns_bad_result();assert(0);}
 if(fault&&!strcmp(fault,"bad-free")){dns_bad_free();assert(0);}
#endif
 if(fault){
  assert(!query("127.0.0.1","60659",12,2,fault));uint32_t head=word(0x14001);
  if(!strcmp(fault,"stale")){call(4,&head,1);call(4,&head,1);assert(0);}
  if(!strcmp(fault,"foreign")){tribes_free_addr_info_w(base+4096,head);assert(0);}
  if(!strcmp(fault,"outside-free")){assert(!tribes_global_free(base,head));call(4,&head,1);assert(0);}
  assert(0);
 }
 compare("127.0.0.1",2,12);compare("::1",23,12);compare("localhost",0,0x402);
 assert(query("not-an-address.invalid","60659",12,0,NULL)==11001&&!word(0x14001));
 assert(query("127.0.0.1","bad-service",12,2,NULL)==11001&&!word(0x14001));
 assert(query("127.0.0.1","60659",0x20,2,NULL)==10022&&!word(0x14001));
 assert(query("127.0.0.1","60659",0,99,NULL)==10047&&!word(0x14001));
 assert(query("127.0.0.1","60659",0,2,NULL)==0);uint32_t head=word(0x14001);call(4,&head,1);
 put(0x13001+28,1);uint32_t a[]={0x11001,0x12001,0x13001,0x14001};assert(call(3,a,4)==11003&&!word(0x14001));
 uint16_t bad[]={0xd800,0};memcpy(memory+0x11001,bad,4);put(0x13001+28,0);assert(call(3,a,4)==10022&&!word(0x14001));
 uint16_t unicode[]={0xd835,0xdd04,0};memcpy(memory+0x11001,unicode,6);put(0x13001,12);assert(call(3,a,4)==11001&&!word(0x14001));
 uint32_t null=0;tribes_set_last_error(0x123);call(4,&null,1);assert(tribes_get_last_error()==0x123);
 assert(!query("localhost","60659",0x402,0,NULL));head=word(0x14001);call(4,&head,1);
 /* Shared resolver registry and guest heap are exercised concurrently. */
 pthread_t t;assert(!pthread_create(&t,NULL,parallel,NULL));for(unsigned i=0;i<8;i++){assert(!tribes_get_addr_info_w(base,0x11001,0x12001,0x13001,0x16000+16*i));head=word(0x16000+16*i);tribes_free_addr_info_w(base,head);}assert(!pthread_join(t,NULL));
 assert(!call(2,&null,0));puts("PASS: real native IPv4/IPv6/localhost DNS, original0x402 hints, owned Windows32 records and UTF16 canonical names, native failures, malformed/valid Unicode, modified-link cleanup, concurrent ownership, stdcall CPU/stack and LastError; component only");return 0;
}
