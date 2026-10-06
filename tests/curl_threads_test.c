#include "../port/core/guest_curl.h"
#include "../port/core/guest_curl_allocator.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void curl_threads_setup(uint64_t),curl_threads_register(unsigned),curl_threads_finish(unsigned *,unsigned *);
extern void curl_threads_body_reset(int),curl_threads_body_check(unsigned);
extern void unsafe_curl_write_callback(void *),unsafe_curl_write_data(void *),unsafe_curl_time_width(void),unsafe_curl_ca_output(void *);
static const char *transfer_url;
static uint64_t base;static uint32_t shared,transferred[4];
static pthread_mutex_t start_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t start_cv=PTHREAD_COND_INITIALIZER;
static unsigned waiting,go;
static _cpu cpu_for(unsigned slot){_cpu c;memset(&c,0,sizeof c);c._pointer_offset=base;c._esp=0x10f000+slot*0x10000;c._ebx=0xbeef0000+slot;c._ecx=0x1234+slot;c._edx=0x5678+slot;c._eflags=0x202;c.stack_bottom=(void *)(uintptr_t)(base+0x100000+slot*0x10000);c.stack_top=(char *)c.stack_bottom+0x10000;return c;}
static uint32_t call(_cpu *c,unsigned target,unsigned n,const uint32_t *args){_cpu before=*c;uint32_t error=0xabcd0000+(c->_ebx&0xffff);tribes_set_last_error(error);uint32_t result=tribes_call_guest_cdecl(c,target,n,args,c->_esp-128);assert(!memcmp(c,&before,sizeof before)&&tribes_get_last_error()==error);return result;}
static void private_value(_cpu *c,uint32_t easy,uint32_t want){uint32_t address=0x180010+(c->_ebx&0xffff)*0x1000,a[]={easy,0x100015,address};assert(!call(c,15,3,a));uint32_t got;memcpy(&got,(void *)(uintptr_t)(base+address),4);assert(got==want);}
static void transfer(_cpu *c,uint32_t easy){
 unsigned slot=c->_ebx&0xffff;uint32_t text=0x190000+slot*0x1000;
 strcpy((char *)(uintptr_t)(base+text),"ca.crt");uint32_t args[]={easy,10065,text};assert(!call(c,14,3,args));
 strcpy((char *)(uintptr_t)(base+text),"TribesPad-runtime-test");args[1]=10018;assert(!call(c,14,3,args));
 snprintf((char *)(uintptr_t)(base+text),1024,"%s/body",transfer_url);args[1]=10002;assert(!call(c,14,3,args));
 args[1]=13;args[2]=10;assert(!call(c,14,3,args));
 args[1]=216;args[2]=16;assert(!call(c,14,3,args));
 args[1]=33;args[2]=1;assert(!call(c,14,3,args));
 uint32_t time[]={easy,30270,0xd202ef00,0}; /* 2081: exercises all32 unsigned low bits. */
 assert(!call(c,14,4,time));
 args[1]=20011;args[2]=7;assert(!call(c,14,3,args));args[1]=10001;args[2]=easy;assert(!call(c,14,3,args));
 args[1]=75;args[2]=1;assert(!call(c,14,3,args));
 curl_threads_body_reset(0);assert(!call(c,22,1,&easy));curl_threads_body_check(40000);
 uint32_t out=0x180000+slot*0x1000,info[]={easy,0x200002,out};assert(!call(c,15,3,info));uint32_t status;memcpy(&status,(void *)(uintptr_t)(base+out),4);assert(status==200);
 /* Verify HTTP conditional behavior rather than just accepting an off_t option. */
 snprintf((char *)(uintptr_t)(base+text),1024,"%s/conditional",transfer_url);args[1]=10002;args[2]=text;assert(!call(c,14,3,args));
 curl_threads_body_reset(0);assert(!call(c,22,1,&easy));curl_threads_body_check(0);assert(!call(c,15,3,info));memcpy(&status,(void *)(uintptr_t)(base+out),4);assert(status==304);
 snprintf((char *)(uintptr_t)(base+text),1024,"%s/body",transfer_url);assert(!call(c,14,3,args));
 curl_threads_body_reset(1);assert(call(c,22,1,&easy)==23);curl_threads_body_check(0);curl_threads_body_reset(0);
 /* Clearing the explicit CA leaves the pinned backend without a default trust
    root; the same endpoint must then fail normal certificate verification. */
 args[1]=10065;args[2]=0;assert(!call(c,14,3,args));assert(call(c,22,1,&easy)==60);curl_threads_body_check(0);
 info[1]=0x10003d;assert(!call(c,15,3,info));memcpy(&status,(void *)(uintptr_t)(base+out),4);assert(!status);
}
static void *worker(void *argument){
 unsigned slot=(unsigned)(uintptr_t)argument;curl_threads_register(slot);_cpu c=cpu_for(slot);uint32_t init[]={1,1,2,3,4,5};
 pthread_mutex_lock(&start_lock);waiting++;pthread_cond_broadcast(&start_cv);while(!go)pthread_cond_wait(&start_cv,&start_lock);pthread_mutex_unlock(&start_lock);
 for(unsigned i=0;i<12;i++){
  assert(!call(&c,10,6,init)); /* Concurrent balanced global references. */
  private_value(&c,shared,0x12345678); /* Same native handle, exclusive calls. */
  uint32_t e=call(&c,12,0,NULL);assert(e);uint32_t set[]={e,10103,0xcafe0000+slot};assert(!call(&c,14,3,set));private_value(&c,e,set[2]);
  uint32_t m=call(&c,16,0,NULL);assert(m);uint32_t option[]={m,10005,e};assert(!call(&c,18,3,option));option[1]=20004;option[2]=6;assert(!call(&c,18,3,option));
  uint32_t pair[]={m,e};assert(!call(&c,19,2,pair));assert(!call(&c,20,2,pair));assert(!call(&c,17,1,&m));
  if(i==11){transfer(&c,e);transferred[slot-1]=e;}else call(&c,13,1,&e);
  uint32_t bad[]={0,10002,0};assert(call(&c,14,3,bad)==43); /* Early-return lease release. */
  call(&c,11,0,NULL);
 }
 return NULL;
}
static TribesCurlAllocator *strict_allocator;
static void *invalid_thread(void *unused){(void)unused;_cpu c=cpu_for(1);TribesCurlFrame f;tribes_curl_allocator_enter(strict_allocator,&c,&f);return NULL;}
int main(int argc,char **argv){
 void *memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(!mprotect((char *)memory+0x100000,0x100000,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
#ifdef CHECKED
 tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif

 curl_threads_setup(base);curl_threads_register(0);_cpu c=cpu_for(0);uint32_t init[]={1,1,2,3,4,5};assert(!call(&c,10,6,init));
 shared=call(&c,12,0,NULL);assert(shared);uint32_t set[]={shared,10103,0x12345678};assert(!call(&c,14,3,set));
 if(argc>1&&argv[1][0]!='/'){
  if(!strcmp(argv[1],"write-callback"))unsafe_curl_write_callback((void *)&puts);
  else if(!strcmp(argv[1],"write-data"))unsafe_curl_write_data((void *)(uintptr_t)(base+0x190000));
  else if(!strcmp(argv[1],"time-width"))unsafe_curl_time_width();
  else if(!strcmp(argv[1],"ca-output"))unsafe_curl_ca_output((void *)(uintptr_t)(base+0x190000));
  else if(!strcmp(argv[1],"cleanup-live"))call(&c,11,0,NULL);
  else if(!strcmp(argv[1],"wrong-base")){c._pointer_offset+=UINT64_C(0x100000000);tribes_curl_easy_init(&c);}
  else if(!strcmp(argv[1],"unframed"))tribes_curl_host_malloc(1);
  else if(!strcmp(argv[1],"fixed-owner")){uint32_t callbacks[]={1,2,3,4,5};strict_allocator=tribes_curl_allocator_create(base,callbacks);assert(strict_allocator);TribesCurlFrame frame;tribes_curl_allocator_enter(strict_allocator,&c,&frame);pthread_t t;assert(!pthread_create(&t,NULL,invalid_thread,NULL)&&!pthread_join(t,NULL));}
  return 99;
 }
 assert(argc==3);transfer_url=argv[2];assert(tribes_filesystem_configure(base,argv[1],argv[1],argv[1]));
 pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));
 pthread_mutex_lock(&start_lock);while(waiting!=4)pthread_cond_wait(&start_cv,&start_lock);go=1;pthread_cond_broadcast(&start_cv);pthread_mutex_unlock(&start_lock);
 for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 for(unsigned i=0;i<4;i++){private_value(&c,transferred[i],0xcafe0001+i);call(&c,13,1,&transferred[i]);}
 call(&c,13,1,&shared);call(&c,11,0,NULL);
 unsigned a,f;curl_threads_finish(&a,&f);printf("PASS: four guest workers, 48 easy/multi lifecycles, serialized shared-handle calls, main/worker handle handoff, nested timer callbacks on correct guest CPU, balanced concurrent global references and early returns, %u allocations/%u frees, zero live mirrors; actual worker HTTPS40000-byte callbacks, conditional304, write failure and TLS trust rejection\n",a,f);return 0;
}
