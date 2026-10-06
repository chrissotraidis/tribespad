#include "../port/core/guest_threads.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_teb.h"
#include "../port/core/guest_modules.h"
#include "../port/core/guest_virtual_memory.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/mman.h>
extern unsigned fixture_notifications(void);
extern void fixture_check(unsigned);
extern void fixture_bad_host(unsigned,uint64_t);
static unsigned char *memory;static uint64_t base;
static uint32_t word(uint32_t p){uint32_t n;memcpy(&n,memory+p,4);return n;}
static uint32_t call(uint32_t target,unsigned n,const uint32_t *args){
 _cpu c={0};c._pointer_offset=base;c._esp=0x80000;c.stack_bottom=memory+0x40000;c.stack_top=memory+0x90000;
 _cpu before=c;uint32_t result=tribes_call_guest(&c,target,n,args,c._esp);assert(!memcmp(&before,&c,sizeof c));return result;
}
static int count_region(uint32_t a,uint32_t s,uint32_t p,void *ctx){(void)a;(void)s;(void)p;(*(unsigned *)ctx)++;return 1;}
int main(int argc,char **argv){
 if(argc>1){struct rlimit r={0,0};assert(!setrlimit(RLIMIT_CORE,&r));}
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));base=(uintptr_t)memory;
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));
 const TribesVirtualMemoryBackend vm_backend={tribes_memory_vm_initialize,tribes_memory_vm_change};
 const void *vm_targets[]={(const void *)tribes_memory_vm_initialize,(const void *)tribes_memory_vm_change};
 tribes_memory_register_targets(vm_targets,2);tribes_memory_external_enable();assert(tribes_virtual_memory_enable_checked(base,&vm_backend));
#endif
 if(argc>1&&strcmp(argv[1],"guard")){fixture_bad_host((unsigned)atoi(argv[1]),base);return 99;}
 unsigned char a[]={0x78,0x56,0x34,0x12},b[]={1,0xef,0xcd,0xab};uint32_t ca=0x100,cb=0x200;
 TribesThreadModule modules[]={{0x100000,0x101,0,4,12,1,a,&ca,0},{0x200000,0x201,1,4,12,1,b,&cb,0}};
 assert(tribes_module_register_image(base,0x200000,0x10000,"C:\\thread.dll",0));
 assert(tribes_threads_configure(base,123,42,0x100000,modules,2));
 assert(!tribes_threads_configure(base,123,42,0x100000,modules,2)&&errno==EALREADY);
 assert(tribes_get_thread_id(0xfffffffe)==42&&tribes_thread_object_live_count()==1);
 assert(tribes_teb_configure(base,0x18000,0x90000,0x40000,123,42,0x19000));
 TribesProcessMetadata metadata={0x10000,0x11000,0x200000,0x12000,0,0x12002,0,0x12004,{1,2,3}};
 assert(tribes_teb_configure_process(base,&metadata));assert(!tribes_teb_configure_process(base,&metadata)&&errno==EALREADY);
 assert(x86_read_fs_dword(0x30)==metadata.peb&&word(0x18030)==metadata.peb);
 assert(word(0x11008)==1&&word(0x11018)==1&&word(0x11020)==3&&word(0x11048)==0x12004);
 /* Exercise the unmodified original readers with both truth values. */
 assert(call(0x500,0,NULL)==0&&call(0x501,0,NULL)==0);
 uint32_t bits=0x80000001;memcpy(memory+0x11008,&bits,4);assert(call(0x500,0,NULL)==1);
 bits=0x100;memcpy(memory+0x10068,&bits,4);assert(call(0x501,0,NULL)==1);
 bits=1;memcpy(memory+0x11008,&bits,4);bits=0;memcpy(memory+0x10068,&bits,4);
 /* Configuration owns immutable copies; poison caller buffers. */
 memset(a,0,4);memset(b,0,4);ca=cb=0;
 if(argc>1){tribes_threads_process_ready(base);uint32_t h=tribes_create_thread(base,0,0,0x300,63,0,0);assert(h);(void)tribes_wait_single_object(h,10000);return 99;}
 enum{N=24};uint32_t handles[N];
 for(unsigned i=0;i<N;i++){
  if(i%3==2){uint32_t image;assert(tribes_get_module_handle_ex_w(base,4,0x200100,0x1000));image=word(0x1000);assert(image==0x200000);}
  uint32_t args[]={0,i==0?0x800000:i==1?0x10001:0,0x300,i,i==1?0x10004:4,0x2000+4*i};
  tribes_set_last_error(777);handles[i]=call(1,6,args);assert(handles[i]&&tribes_get_last_error()==777);
  assert(call(4,1,&handles[i])==word(0x2000+4*i));uint32_t query[]={handles[i],0x1000};assert(call(3,2,query)&&word(0x1000)==259);
  assert(tribes_wait_single_object(handles[i],0)==258);
 }
 assert(fixture_notifications()==0);
 /* Resumed worker still waits for process attach completion. */
 assert(call(2,1,&handles[0])==1);assert(tribes_wait_single_object(handles[0],15)==258&&fixture_notifications()==0);
 tribes_threads_process_ready(base);
 for(unsigned i=1;i<N;i++){
  uint32_t original=handles[i];assert(tribes_duplicate_handle(base,UINT32_MAX,original,UINT32_MAX,0x1000,0,0,2));handles[i]=word(0x1000);
  assert(tribes_close_handle(original));assert(call(2,1,&handles[i])==1);assert(call(2,1,&handles[i])==0);
 }
 for(unsigned i=0;i<N;i++){
  assert(tribes_wait_single_object(handles[i],10000)==0&&tribes_wait_single_object(handles[i],0)==0);
  uint32_t query[]={handles[i],0x1000};assert(call(3,2,query)&&word(0x1000)==0x9000+i);assert(tribes_close_handle(handles[i]));
 }
 /* Drop the only public handle while its running-owner reference remains. */
 uint32_t early=tribes_create_thread(base,0,0,0x300,N,4,0);assert(early&&tribes_resume_thread(early)==1&&tribes_close_handle(early));
 for(unsigned retry=0;tribes_guest_threads_live_count()&&retry<1000;retry++){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
 assert(fixture_notifications()==8*(N+1));fixture_check(N+1);
 assert(tribes_guest_threads_live_count()==0&&tribes_thread_object_live_count()==1);
 /* Long-function work owns no public thread handle. Verify real independent
    callbacks, TLS/FLS cleanup and reclamation after all three exit paths. */
 uint32_t queued[]={0,0,0x10};assert(!call(7,3,queued)&&tribes_get_last_error()==87);
 for(unsigned i=N+1;i<N+4;i++){
  if(i%3==2)assert(tribes_get_module_handle_ex_w(base,4,0x200100,0x1000));
  queued[0]=0x300;queued[1]=i;tribes_set_last_error(778);
  assert(call(7,3,queued)==1&&tribes_get_last_error()==778);
 }
 for(unsigned retry=0;tribes_guest_threads_live_count()&&retry<1000;retry++){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
 assert(fixture_notifications()==8*(N+4));fixture_check(N+4);
 assert(tribes_guest_threads_live_count()==0&&tribes_thread_object_live_count()==1);
 unsigned regions=0;assert(tribes_virtual_memory_visit(base,count_region,&regions)&&regions==0);
 uint32_t invalid[]={0,0,0x300,0,1,0};assert(!call(1,6,invalid)&&tribes_get_last_error()==87);
 invalid[1]=UINT32_MAX;invalid[4]=0;assert(!call(1,6,invalid)&&tribes_get_last_error()==8);
 uint32_t bad=0x123;assert(call(2,1,&bad)==UINT32_MAX&&tribes_get_last_error()==6);
 puts("PASS: 25 native guest threads and 3 queued long-function workers; suspend/process-ready gates, independent TEB/TLS, ordered serialized callbacks, FLS cleanup, three exit paths, duplicate/early-close/wait lifetime and VM reclamation");return 0;
}
