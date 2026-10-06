#include "../port/core/guest_callback.h"
#include "../port/core/guest_teb.h"
#include "../port/core/guest_heap.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_memory_checks.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
extern void tribes_enter(_cpu *,uint32_t),tribes_return_to_host(void);
extern void tbimp_RaiseException(void),tbimp_TestHandler(void),tbimp_WaitForMultipleObjects(void),tbimp_SetThreadPriority(void);
void *tribes_lookup(uint32_t a){switch(a){case 1:return tbimp_RaiseException;case 2:return tbimp_TestHandler;case 3:return tbimp_WaitForMultipleObjects;case 4:return tbimp_SetThreadPriority;case 0xfffff000:return tribes_return_to_host;default:abort();}}
static unsigned char *memory;static int edit_context;static unsigned handler_calls;static atomic_uint observed,done;static uint32_t pair[2],result;static TribesThreadObject *thread;
void tribes_event_wait_observation(void){atomic_fetch_add(&observed,1);}
static void pause_ms(unsigned n){struct timespec t={0,n*1000000};nanosleep(&t,NULL);}
static void await_value(atomic_uint *p,unsigned v){for(unsigned i=0;i<2000&&atomic_load(p)<v;i++)pause_ms(1);assert(atomic_load(p)>=v);}
uint32_t test_handler(_cpu *cpu,uint32_t record,uint32_t frame,uint32_t context,uint32_t dispatcher){
 assert(!dispatcher&&frame==0x1f0080);uint32_t *r=(void *)(memory+record),*c=(void *)(memory+context);assert(cpu->_pointer_offset==(uintptr_t)memory&&r[0]==0x406d1388&&r[1]==0&&r[4]==4&&r[5]==0x1000&&r[7]==74);assert(c[0]==0x10003&&c[46]==0xfffff000&&c[49]==0x1f0000);handler_calls++;if(edit_context)c[46]++;return 0;
}
static uint32_t invoke(unsigned target,unsigned n,uint32_t *args){_cpu cpu={0};cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x1f0000-4*n;cpu.stack_bottom=memory+0x180000;cpu.stack_top=memory+0x200000;cpu._eax=123;cpu._eflags=0x246;memcpy(memory+cpu._esp,args,4*n);_cpu before=cpu;tribes_enter(&cpu,target);if(target!=1)before._eax=cpu._eax;before._esp=0x1f0000;assert(!memcmp(&cpu,&before,sizeof cpu));return cpu._eax;}
static void *waiter(void *unused){(void)unused;result=tribes_wait_multiple_objects((uintptr_t)pair-128,2,128,0,UINT32_MAX);if(result==1||result==129)assert(tribes_release_mutex(pair[1]));atomic_store(&done,1);return NULL;}
static void launch_waiter(pthread_t *t){atomic_store(&observed,0);atomic_store(&done,0);assert(!pthread_create(t,NULL,waiter,NULL));await_value(&observed,1);}
static void *priority_worker(void *unused){(void)unused;tribes_thread_object_set_current(thread);atomic_store(&observed,1);await_value(&done,1);int policy;struct sched_param p;assert(!pthread_getschedparam(pthread_self(),&policy,&p));assert(policy==SCHED_OTHER&&p.sched_priority==sched_get_priority_max(SCHED_OTHER));tribes_thread_object_finish(thread,0);tribes_thread_object_set_current(NULL);return NULL;}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory+0x100000,0x100000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));assert(tribes_teb_configure((uintptr_t)memory,0x100000,0x200000,0x180000,51,73,0x101000));uint32_t frame[]={UINT32_MAX,2};memcpy(memory+0x1f0080,frame,8);x86_write_fs_dword(0,0x1f0080);strcpy((char *)memory+0x102080,"MSS Timer");uint32_t packet[]={0x1000,0x102080,74,0};memcpy(memory+0x102000,packet,16);uint32_t args[]={0x406d1388,0,4,0x102000};
 if(argc>1&&!strcmp(argv[1],"code"))args[0]=0xe1234567;
 if(argc>1&&!strcmp(argv[1],"context"))edit_context=1;
 if(argc>1&&!strcmp(argv[1],"unreadable"))assert(tribes_memory_permissions_set(0x102000,4096,0));
 invoke(1,4,args);assert(handler_calls==1&&x86_read_fs_dword(0)==0x1f0080);
 pair[0]=tribes_create_event(0,0,1,0,0);pair[1]=tribes_create_mutex(0,0,1,0);assert(pair[0]&&pair[1]);memcpy(memory+0x103000,pair,8);uint32_t wait[]={2,0x103000,0,0};assert(invoke(3,4,wait)==1);assert(tribes_release_mutex(pair[1]));
 assert(tribes_set_event(pair[0]));assert(invoke(3,4,wait)==0);assert(tribes_reset_event(pair[0]));
 pthread_t t;launch_waiter(&t);assert(tribes_set_event(pair[0])&&tribes_reset_event(pair[0]));await_value(&done,1);assert(!pthread_join(t,NULL)&&result==0);
 launch_waiter(&t);assert(tribes_release_mutex(pair[1]));await_value(&done,1);assert(!pthread_join(t,NULL)&&result==1);assert(tribes_close_handle(pair[0])&&tribes_close_handle(pair[1]));
 uint32_t h;thread=tribes_thread_object_create(74,0,&h);assert(thread);atomic_store(&observed,0);atomic_store(&done,0);assert(!pthread_create(&t,NULL,priority_worker,NULL));await_value(&observed,1);uint32_t priority[]={h,15};assert(invoke(4,2,priority));atomic_store(&done,1);assert(!pthread_join(t,NULL));assert(tribes_close_handle(h));priority[0]=1234;assert(!invoke(4,2,priority)&&tribes_get_last_error()==6);
 puts("PASS: real SEH callback/continuation ABI and frame retention; event/mutex wait-any selection, event reset wake and mutex acquisition; native thread-priority application");return 0;
}
