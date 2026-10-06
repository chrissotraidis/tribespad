#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/native_handles.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
extern void tbimp_CreateMutexA(void),tbimp_CloseHandle(void),tbimp_WaitForSingleObject(void),tbimp_ReleaseMutex(void),tbimp_DuplicateHandle(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t a){switch(a){case 1:return tbimp_CreateMutexA;case 2:return tbimp_CloseHandle;case 3:return tbimp_WaitForSingleObject;case 4:return tbimp_ReleaseMutex;case 5:return tbimp_DuplicateHandle;case 0xfffff000:return tribes_return_to_host;default:abort();}}
_Noreturn void tribes_unimplemented_dinput(_cpu *c,uint32_t m){(void)c;(void)m;abort();}
static _Thread_local unsigned char memory[16384];
static uint32_t call(uint32_t target,unsigned n,const uint32_t *args){
 _cpu c;memset(&c,0xa5,sizeof c);c._pointer_offset=(uintptr_t)memory;c._esp=8192-4*n;memset(memory+8000,0x69,300);memcpy(memory+c._esp,args,n*4);_cpu before=c;
 tribes_enter(&c,target);before._eax=c._eax;before._esp=8192;assert(!memcmp(&before,&c,sizeof c));assert(!memcmp(memory+8192-4*n,args,n*4));assert(memory[8192]==0x69&&memory[8192-4*n-5]==0x69);return c._eax;
}
static uint32_t create(uint32_t initial){uint32_t a[]={0,initial,0};return call(1,3,a);}
static uint32_t waiton(uint32_t h,uint32_t ms){uint32_t a[]={h,ms};return call(3,2,a);}
static uint32_t release(uint32_t h){return call(4,1,&h);}
static void closeh(uint32_t h){assert(call(2,1,&h));}
static uint32_t shared,counter;static atomic_int ready,proceed;
static void *contend(void *p){(void)p;for(int i=0;i<1000;i++){assert(waiton(shared,UINT32_MAX)==0);unsigned v=counter;counter=v+1;assert(release(shared));}return NULL;}
static void *foreign(void *p){(void)p;assert(!release(shared)&&tribes_get_last_error()==288);assert(waiton(shared,0)==258);struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);assert(waiton(shared,20)==258);clock_gettime(CLOCK_MONOTONIC,&b);assert((b.tv_sec-a.tv_sec)*1000000000LL+b.tv_nsec-a.tv_nsec>=19000000);atomic_store(&ready,1);assert(waiton(shared,UINT32_MAX)==0);atomic_store(&proceed,1);assert(release(shared));return NULL;}
static void *abandon(void *p){assert(waiton(shared,UINT32_MAX)==0);assert(waiton(shared,0)==0);if(p){uint32_t h;TribesThreadObject *t=tribes_thread_object_create(77,0,&h);assert(t);tribes_thread_object_set_current(t);tribes_thread_object_finish(t,9);tribes_thread_object_set_current(NULL);closeh(h);}return NULL;}
int main(void){
 shared=create(1);assert(shared);assert(waiton(shared,0)==0);assert(tribes_fd_borrow(shared)==-1&&tribes_get_last_error()==6);uint32_t status;assert(tribes_fd_borrow_nt(shared,&status)==-1);
 pthread_t t;assert(!pthread_create(&t,NULL,foreign,NULL));while(!atomic_load(&ready)){}assert(release(shared));assert(!atomic_load(&proceed));assert(release(shared));assert(!pthread_join(t,NULL));assert(atomic_load(&proceed));
 assert(!release(shared)&&tribes_get_last_error()==288);
 uint32_t args[]={UINT32_MAX,shared,UINT32_MAX,256,0,0,2};assert(call(5,7,args));uint32_t copy;memcpy(&copy,memory+256,4);closeh(shared);shared=copy;
 pthread_t group[4];for(int i=0;i<4;i++)assert(!pthread_create(&group[i],NULL,contend,NULL));for(int i=0;i<4;i++)assert(!pthread_join(group[i],NULL));assert(counter==4000);
 for(int guest=0;guest<2;guest++){assert(!pthread_create(&t,NULL,abandon,(void *)(uintptr_t)guest));assert(!pthread_join(t,NULL));assert(waiton(shared,0)==128);assert(waiton(shared,0)==0);assert(release(shared)&&release(shared));}
 closeh(shared);assert(waiton(shared,0)==UINT32_MAX&&tribes_get_last_error()==6);assert(!release(shared)&&tribes_get_last_error()==6);
 uint32_t sem=tribes_create_semaphore(0,0,1,1,0);assert(sem);assert(!release(sem)&&tribes_get_last_error()==6);assert(waiton(sem,0)==0);closeh(sem);
 for(int i=0;i<1000;i++){uint32_t h=create(i&1);assert(h);tribes_set_last_error(123);assert(waiton(h,0)==0&&tribes_get_last_error()==123);assert(release(h));if(i&1)assert(release(h));closeh(h);}
 puts("PASS: emitted mutex ABIs; recursive/foreign ownership, timed and infinite contention, 4000 serialized updates, duplicated lifetime, native/guest thread abandonment, type/closed-handle errors, 1000 lifetimes, CPU/flags/stack preservation");return 0;
}
