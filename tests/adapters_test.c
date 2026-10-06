#include "../port/core/guest_adapters.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
#ifdef CHECKED_TEST
extern void adapters_guest_get(void *),adapters_guest_free(void *),adapters_wrong_get(void *),adapters_wrong_free(void *);
#endif
static unsigned char *memory;static uint64_t base;
static uint32_t read32(uint32_t at){uint32_t n;memcpy(&n,memory+at,4);return n;}
static void write32(uint32_t at,uint32_t n){memcpy(memory+at,&n,4);}
static uint32_t call(uint32_t family,uint32_t flags,uint32_t reserved,uint32_t output,uint32_t size){
 uint32_t args[]={family,flags,reserved,output,size};_cpu cpu;memset(&cpu,0xa9,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0xc003;memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;
 tribes_set_last_error(0x12345678);tribes_enter(&cpu,1);expected._eax=cpu._eax;expected._esp+=sizeof args;
 assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0xc003,args,sizeof args)&&tribes_get_last_error()==0x12345678);return cpu._eax;
}
static int selected(const struct ifaddrs *p,unsigned family){return p->ifa_addr&&((p->ifa_addr->sa_family==AF_INET&&(family==0||family==2))||(p->ifa_addr->sa_family==AF_INET6&&(family==0||family==23)));}
static unsigned validate(unsigned family,uint32_t output,uint32_t size){
 struct ifaddrs *head;assert(!getifaddrs(&head));unsigned expected=0,actual=0;
 for(struct ifaddrs *p=head;p;p=p->ifa_next)if(selected(p,family))expected++;
 uint32_t end=output+size;
 for(uint32_t aa=output;aa;aa=read32(aa+8)){
  assert(aa>=output&&aa+376<=end&&read32(aa)==376);uint32_t name=read32(aa+12),wide=read32(aa+40);assert(name>=output&&name<end&&wide>=output&&wide<end);
  const char *n=(const char *)memory+name;size_t length=strnlen(n,end-name);assert(length<end-name&&wide+2*(length+1)<=end);
  for(unsigned i=0;i<=length;i++)assert(memory[wide+2*i]==(unsigned char)n[i]&&!memory[wide+2*i+1]);
  assert(read32(aa+4)==if_nametoindex(n));assert(!read32(aa+20)&&!read32(aa+24)&&!read32(aa+28));
  for(struct ifaddrs *p=head;p;p=p->ifa_next)if(p->ifa_addr&&!strcmp(p->ifa_name,n)){
   if(p->ifa_addr->sa_family==AF_LINK){struct sockaddr_dl *dl=(void *)p->ifa_addr;assert(read32(aa+52)==dl->sdl_alen);assert(!memcmp(memory+aa+44,LLADDR(dl),dl->sdl_alen));if(p->ifa_data)assert(read32(aa+60)==((struct if_data *)p->ifa_data)->ifi_mtu);}
   assert(read32(aa+68)==((p->ifa_flags&IFF_UP)?((p->ifa_flags&IFF_RUNNING)?1:5):2));
  }
  for(uint32_t ua=read32(aa+16);ua;ua=read32(ua+8)){
   assert(ua>=output&&ua+48<=end&&read32(ua)==48);uint32_t sa=read32(ua+12),len=read32(ua+16);assert(sa>=output&&sa+len<=end);unsigned fam=memory[sa];assert((fam==2&&len==16)||(fam==23&&len==28));assert(!family||family==fam);int found=0;
   for(struct ifaddrs *p=head;p;p=p->ifa_next)if(selected(p,family)&&!strcmp(p->ifa_name,n)){
    const void *address;const unsigned char *mask;unsigned bytes;
    if(fam==2&&p->ifa_addr->sa_family==AF_INET){address=&((struct sockaddr_in *)p->ifa_addr)->sin_addr;mask=(void *)&((struct sockaddr_in *)p->ifa_netmask)->sin_addr;bytes=4;}
    else if(fam==23&&p->ifa_addr->sa_family==AF_INET6){address=&((struct sockaddr_in6 *)p->ifa_addr)->sin6_addr;mask=(void *)&((struct sockaddr_in6 *)p->ifa_netmask)->sin6_addr;bytes=16;}
    else continue;
    if(memcmp(memory+sa+(fam==2?4:8),address,bytes))continue;
    if(fam==23)assert(read32(sa+24)==((struct sockaddr_in6 *)p->ifa_addr)->sin6_scope_id);
    unsigned bits=0;for(unsigned i=0;i<bytes;i++)bits+=(unsigned)__builtin_popcount(mask[i]);assert(memory[ua+44]==bits);found=1;break;
   }
   assert(found);assert(++actual<=expected);
  }
 }
 freeifaddrs(head);assert(actual==expected);return actual;
}
static void *worker(void *arg){uint32_t at=0x40000+(uint32_t)(uintptr_t)arg*0x20000,sz=at+0x18000;for(unsigned i=0;i<16;i++){write32(sz,0x18000);tribes_set_last_error(at);assert(!tribes_get_adapters_addresses(base,0,14,0,at,sz));assert(read32(sz)==0x18000&&tribes_get_last_error()==at);}return NULL;}
#ifdef CHECKED_TEST
static void rejected(unsigned mode){
 unsigned char saved[65536];memcpy(saved,memory+0x10000,sizeof saved);fflush(stdout);pid_t child=fork();assert(child>=0);
 if(!child){
  if(mode==0){write32(0x1000,0x10000);assert(tribes_memory_permissions_set(0x18000,4096,1));tribes_get_adapters_addresses(base,0,14,0,0x17ff8,0x1000);}
  else if(mode==1){assert(tribes_memory_permissions_set(0x18000,4096,0));tribes_get_adapters_addresses(base,0,14,0,0,0x18000);}
  else if(mode==2){assert(tribes_memory_permissions_set(0x18000,4096,1));tribes_get_adapters_addresses(base,0,14,0,0,0x18000);}
  else if(mode==3)tribes_get_adapters_addresses(base,0,0,0,0,0x1000);
  else if(mode==4)adapters_guest_get(memory+0x18000);
  else if(mode==5)adapters_guest_free(memory+0x18000);
  else{void *host=NULL;if(mode==6)adapters_wrong_get(&host);else adapters_wrong_free(&host);}
  _exit(90);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(saved,memory+0x10000,sizeof saved));
}
#endif
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 assert(call(0,14,0,0,0)==87);assert(call(99,14,0,0,0x1000)==87);assert(call(0,14,1,0,0x1000)==87);assert(call(0,14,0,0,0xfffffffe)==87);
 unsigned families[]={0,2,23},count=0;
 for(unsigned i=0;i<3;i++){
  write32(0x1001,UINT32_MAX);assert(call(families[i],14,0,0,0x1001)==111);uint32_t size=read32(0x1001);assert(size>376&&size<0x10000);
  memset(memory+0x10000,0x5a,0x10000);write32(0x1001,1);assert(call(families[i],14,0,0x10000,0x1001)==111);for(unsigned j=0;j<0x10000;j++)assert(memory[0x10000+j]==0x5a);
  write32(0x1001,0x10000-2);assert(!call(families[i],14,0,0x10001,0x1001));assert(read32(0x1001)==0x10000-2&&memory[0x10000]==0x5a&&memory[0x1ffff]==0x5a);count+=validate(families[i],0x10001,read32(0x1001));
 }
 write32(0x1000,0x10000);assert(call(0,14,0,0xfffffff0,0x1000)==87);
 pthread_t threads[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
#ifdef CHECKED_TEST
 for(unsigned mode=0;mode<8;mode++)rejected(mode);
#endif
 printf("PASS: %u real host address comparisons across 3 families, emitted ABI/CPU/LastError, buffer negotiation, 64 concurrent snapshots, guarded output/size/host contracts when enabled\n",count);return 0;
}
