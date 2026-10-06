#include "../port/core/guest_display.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include "../port/apple/display.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;static uint32_t monitor,expected[10];
static void put32(uint32_t p,uint32_t n){memcpy(memory+p,&n,4);}
static uint32_t call(unsigned id,uint32_t a,uint32_t b){
 _cpu c;memset(&c,0xa5,sizeof c);c._pointer_offset=base;c._esp=0xc003;uint32_t args[]={a,b};memcpy(memory+c._esp,args,8);_cpu before=c;
 tribes_enter(&c,id);before._eax=c._eax;before._esp+=8;assert(!memcmp(&before,&c,sizeof c));assert(!memcmp(memory+0xc003,args,8));return c._eax;
}
static void *worker(void *arg){
 uint32_t p=0x10000+(uint32_t)(uintptr_t)arg*4096;
 for(unsigned n=0;n<32;n++){
  put32(p,40);tribes_set_last_error(p);assert(tribes_get_monitor_info_w(base,monitor,p));assert(!memcmp(memory+p,expected,40)&&tribes_get_last_error()==p);
 }
 return NULL;
}
#if defined(CHECKED_TEST) && !defined(SIMULATOR_TEST)
static void reject(unsigned mode){
 pid_t child=fork();assert(child>=0);if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);
  if(mode==0){put32(0x20000,40);assert(tribes_memory_permissions_set(0x20000,4096,1));tribes_get_monitor_info_a(base,monitor,0x20000);}
  if(mode==1){put32(0x1ffec,40);assert(tribes_memory_permissions_set(0x20000,4096,1));tribes_get_monitor_info_a(base,monitor,0x1ffec);}
  if(mode==2){assert(tribes_memory_permissions_set(0x20000,4096,0));tribes_get_monitor_info_a(base,monitor,0x20000);}
  if(mode==3)tribes_monitor_from_window(base,0xc1000001,1);_exit(99);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
#endif
int main(void){
 TBAppleDisplay display;assert(tb_apple_display_snapshot(&display));
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));base=(uintptr_t)memory;
 assert(tribes_display_configure(base,display.width,display.height));assert(!tribes_display_configure_work_area(base,-1,0,1,1));
 uint32_t window=tribes_get_desktop_window(base);monitor=tribes_monitor_from_window(base,window,1);assert(monitor&&monitor!=window);
 put32(0x1001,40);assert(!tribes_get_monitor_info_a(base,monitor,0x1001)&&tribes_get_last_error()==50);
 assert(tribes_display_configure_work_area(base,display.work_left,display.work_top,display.work_right,display.work_bottom));assert(!tribes_display_configure_work_area(base,0,0,1,1));
 assert(tribes_display_configure(base+0x1000,4,4));uint32_t foreign=tribes_monitor_from_window(base+0x1000,tribes_get_desktop_window(base+0x1000),1);assert(foreign!=monitor);
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 tribes_set_last_error(123);for(unsigned f=0;f<3;f++){assert(call(1,window,f)==monitor);assert(call(1,0,f)==(f?monitor:0));}assert(tribes_get_last_error()==123);assert(!call(1,window,3)&&tribes_get_last_error()==87);
 uint32_t fields[]={40,0,0,display.width,display.height,(uint32_t)display.work_left,(uint32_t)display.work_top,(uint32_t)display.work_right,(uint32_t)display.work_bottom,1};memcpy(expected,fields,40);
 for(unsigned wide=0;wide<2;wide++)for(uint32_t size=0;size<113;size++){
  memset(memory+0x1000,0xa5,256);put32(0x1001,size);tribes_set_last_error(123);unsigned result=call(wide?3:2,monitor,0x1001),valid=size==40||size==(wide?104:72);assert(result==valid);
  if(valid){fields[0]=size;assert(!memcmp(memory+0x1001,fields,40)&&tribes_get_last_error()==123);
   if(size!=40){const char *name="\\\\.\\DISPLAY1";for(unsigned i=0;i<32;i++){assert(memory[0x1001+40+i*(wide?2:1)]==(i<strlen(name)?name[i]:0));if(wide)assert(!memory[0x1001+41+2*i]);}}
  }else assert(tribes_get_last_error()==87&&!memcmp(memory+0x1001,&size,4));
  assert(memory[0x1000]==0xa5);for(unsigned i=valid?size:4;i<255;i++)assert(memory[0x1001+i]==0xa5);
 }
 assert(!call(2,window,0x1001)&&tribes_get_last_error()==1461);assert(!call(3,foreign,0x1001)&&tribes_get_last_error()==1461);
 assert(!call(2,monitor,0)&&tribes_get_last_error()==87);assert(!call(2,monitor,UINT32_MAX-1)&&tribes_get_last_error()==87);
#if defined(CHECKED_TEST) && !defined(SIMULATOR_TEST)
 for(unsigned i=0;i<4;i++)reject(i);
#endif
 pthread_t workers[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&workers[i],NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(workers[i],NULL));
 printf("PASS: primary Apple display %ux%u work=%d,%d,%d,%d; 226 A/W size cases, monitor token/base isolation, ABI/LastError preservation, 128 concurrent snapshots and checked guards\n",display.width,display.height,display.work_left,display.work_top,display.work_right,display.work_bottom);
}
