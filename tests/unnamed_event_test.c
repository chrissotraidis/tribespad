#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t),tribes_return_to_host(void);
extern void tbimp_CreateEventA(void),tbimp_SetEvent(void),tbimp_ResetEvent(void),tbimp_WaitForSingleObject(void),tbimp_CloseHandle(void),tbimp_DuplicateHandle(void);
void *tribes_lookup(uint32_t a){switch(a){case 1:return tbimp_CreateEventA;case 2:return tbimp_SetEvent;case 3:return tbimp_ResetEvent;case 4:return tbimp_WaitForSingleObject;case 5:return tbimp_CloseHandle;case 6:return tbimp_DuplicateHandle;case 0xfffff000:return tribes_return_to_host;default:abort();}}
static unsigned char *memory;static atomic_uint waiting,completed;static uint32_t shared;static atomic_uint last_result;
void tribes_event_wait_observation(void){atomic_fetch_add(&waiting,1);}
static void pause_ms(unsigned ms){struct timespec t={0,(long)ms*1000000};nanosleep(&t,NULL);}
static void await_count(atomic_uint *v,unsigned n){for(unsigned i=0;i<2000&&atomic_load(v)!=n;i++)pause_ms(1);assert(atomic_load(v)==n);}
static uint32_t invoke(uint32_t target,unsigned n,const uint32_t *args){_cpu c={0};c._pointer_offset=(uintptr_t)memory;c._esp=0x180000-4*n;c._eax=123;c._eflags=0x246;memcpy(memory+c._esp,args,4*n);_cpu before=c;tribes_enter(&c,target);before._eax=c._eax;before._esp=0x180000;assert(!memcmp(&before,&c,sizeof c));return c._eax;}
static uint32_t create(unsigned manual,unsigned initial){uint32_t a[]={0,manual,initial,0};return invoke(1,4,a);}
static uint32_t waiton(uint32_t h,unsigned ms){uint32_t a[]={h,ms};return invoke(4,2,a);}
static void *worker(void *unused){(void)unused;uint32_t result=tribes_wait_single_object(shared,UINT32_MAX);atomic_store(&last_result,result);atomic_fetch_add(&completed,1);return NULL;}
static void begin(pthread_t *threads,unsigned count){atomic_store(&waiting,0);atomic_store(&completed,0);for(unsigned i=0;i<count;i++)assert(!pthread_create(threads+i,NULL,worker,NULL));await_count(&waiting,count);}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory+0x100000,0x100000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));
 uint32_t h=create(1,0);assert(h&&waiton(h,0)==258);struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);assert(waiton(h,20)==258);clock_gettime(CLOCK_MONOTONIC,&b);assert((b.tv_sec-a.tv_sec)*1000000000LL+b.tv_nsec-a.tv_nsec>=19000000);
 assert(invoke(2,1,&h)&&waiton(h,0)==0&&waiton(h,0)==0);assert(invoke(3,1,&h)&&waiton(h,0)==258);
 uint32_t d[]={UINT32_MAX,h,UINT32_MAX,0x102000,0,0,2};
 if(argc>1&&!strcmp(argv[1],"readonly"))assert(tribes_memory_permissions_set(0x102000,4096,1));
 assert(invoke(6,7,d));uint32_t copy;memcpy(&copy,memory+0x102000,4);assert(invoke(5,1,&h));h=copy;assert(invoke(2,1,&h)&&waiton(h,0)==0);assert(tribes_fd_borrow(h)==-1&&tribes_get_last_error()==6);uint32_t status;assert(tribes_fd_borrow_nt(h,&status)==-1&&status==0xc0000024);assert(!tribes_release_semaphore(0,h,1,0)&&tribes_get_last_error()==6);assert(!tribes_release_mutex(h)&&tribes_get_last_error()==6);assert(invoke(5,1,&h));assert(!invoke(2,1,&h)&&tribes_get_last_error()==6);
 h=create(0,1);assert(waiton(h,0)==0&&waiton(h,0)==258);assert(invoke(2,1,&h)&&invoke(2,1,&h)&&waiton(h,0)==0&&waiton(h,0)==258);assert(invoke(5,1,&h));
 pthread_t threads[4];shared=create(1,0);begin(threads,4);assert(tribes_set_event(shared)&&tribes_reset_event(shared));await_count(&completed,4);for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(atomic_load(&last_result)==0&&waiton(shared,0)==258);assert(tribes_close_handle(shared));
 shared=create(0,0);begin(threads,2);assert(tribes_set_event(shared)&&tribes_reset_event(shared));await_count(&completed,1);pause_ms(10);assert(atomic_load(&completed)==1);assert(tribes_set_event(shared));await_count(&completed,2);for(unsigned i=0;i<2;i++)assert(!pthread_join(threads[i],NULL));assert(atomic_load(&last_result)==0&&waiton(shared,0)==258);assert(tribes_close_handle(shared));
 shared=create(1,0);begin(threads,1);assert(tribes_close_handle(shared));await_count(&completed,1);assert(!pthread_join(threads[0],NULL)&&atomic_load(&last_result)==UINT32_MAX);
 uint32_t sem=tribes_create_semaphore(0,0,0,1,0);assert(!tribes_set_event(sem)&&tribes_get_last_error()==6);assert(!tribes_reset_event(sem)&&tribes_get_last_error()==6);assert(tribes_close_handle(sem));
 puts("PASS: event stdcall ABI, real timed wait, manual broadcast and retained wake after reset, auto-reset single wake/coalescing, duplicate lifetime, final-close wake and typed-handle errors");return 0;
}
