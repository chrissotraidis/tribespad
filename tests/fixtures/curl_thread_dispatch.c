#include "../../port/core/guest_curl.h"
#include "../../port/core/guest_curl_allocator.h"
#include "../../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static uint64_t memory_base;
static _Thread_local unsigned thread_slot;
static _Thread_local int registered;
static _Thread_local unsigned char body[65536];
static _Thread_local unsigned body_length;
static _Thread_local int fail_write;
static pthread_mutex_t allocations_lock=PTHREAD_MUTEX_INITIALIZER;
typedef struct Block {uint32_t address,size;struct Block *next;} Block;
static Block *blocks;
static unsigned live,allocations,frees,callbacks[5],timers[5];
void curl_threads_setup(uint64_t base){memory_base=base;}
void curl_threads_register(unsigned slot){assert(slot<5);thread_slot=slot;registered=1;}
static void check(_cpu *c){
 assert(registered&&c->_pointer_offset==memory_base&&c->_ebx==0xbeef0000+thread_slot);
 assert((uintptr_t)c->stack_bottom==memory_base+0x100000+thread_slot*0x10000);
 assert((uintptr_t)c->stack_top==(uintptr_t)c->stack_bottom+0x10000);
 callbacks[thread_slot]++;
}
static Block **find(uint32_t address){Block **p=&blocks;while(*p&&(*p)->address!=address)p=&(*p)->next;assert(*p);return p;}
static uint32_t allocate(_cpu *c,uint32_t size){
 uint32_t p=tribes_global_alloc(c->_pointer_offset,0,size);assert(p);Block *b=malloc(sizeof *b);assert(b);
 *b=(Block){p,size,blocks};blocks=b;live++;allocations++;memset((void *)(uintptr_t)(memory_base+p),0x5a,size);return p;
}
uint32_t curl_threads_malloc(_cpu *c,uint32_t size){pthread_mutex_lock(&allocations_lock);check(c);uint32_t p=allocate(c,size);pthread_mutex_unlock(&allocations_lock);return p;}
void curl_threads_free(_cpu *c,uint32_t p){
 pthread_mutex_lock(&allocations_lock);check(c);
 if(p){Block **slot=find(p),*b=*slot;*slot=b->next;assert(!tribes_global_free(c->_pointer_offset,p));free(b);live--;frees++;}
 pthread_mutex_unlock(&allocations_lock);
}
uint32_t curl_threads_realloc(_cpu *c,uint32_t p,uint32_t size){
 pthread_mutex_lock(&allocations_lock);check(c);uint32_t q=allocate(c,size);
 if(p){Block **slot=find(p),*b=*slot;memcpy((void *)(uintptr_t)(memory_base+q),(void *)(uintptr_t)(memory_base+p),size<b->size?size:b->size);*slot=b->next;assert(!tribes_global_free(c->_pointer_offset,p));free(b);live--;frees++;}
 pthread_mutex_unlock(&allocations_lock);return q;
}
uint32_t curl_threads_strdup(_cpu *c,uint32_t source){
 pthread_mutex_lock(&allocations_lock);check(c);const char *s=(const char *)(uintptr_t)(memory_base+source);uint32_t n=(uint32_t)strlen(s)+1,p=allocate(c,n);memcpy((void *)(uintptr_t)(memory_base+p),s,n);pthread_mutex_unlock(&allocations_lock);return p;
}
uint32_t curl_threads_calloc(_cpu *c,uint32_t n,uint32_t size){assert(!size||n<=UINT32_MAX/size);uint32_t p=curl_threads_malloc(c,n*size);memset((void *)(uintptr_t)(memory_base+p),0,n*size);return p;}
uint32_t curl_threads_timer(_cpu *c,uint32_t multi,uint32_t timeout,uint32_t easy){
 (void)multi;(void)timeout;pthread_mutex_lock(&allocations_lock);check(c);timers[thread_slot]++;pthread_mutex_unlock(&allocations_lock);
 uint32_t out=0x1a0000+thread_slot*0x1000,args[]={easy,0x100015,out};
 assert(!tribes_call_guest_cdecl(c,15,3,args,c->_esp-128));uint32_t private;memcpy(&private,(void *)(uintptr_t)(memory_base+out),4);assert(private==0xcafe0000+thread_slot);
 return 0;
}
uint32_t curl_threads_write(_cpu *c,uint32_t buffer,uint32_t size,uint32_t count,uint32_t easy){
 pthread_mutex_lock(&allocations_lock);check(c);pthread_mutex_unlock(&allocations_lock);
 if(fail_write)return 0;
 assert(size==1&&count<=sizeof body-body_length);
 memcpy(body+body_length,(void *)(uintptr_t)(memory_base+buffer),count);body_length+=count;
 uint32_t output=0x1a0000+thread_slot*0x1000,args[]={easy,0x100015,output};
 assert(!tribes_call_guest_cdecl(c,15,3,args,c->_esp-128));uint32_t value;memcpy(&value,(void *)(uintptr_t)(memory_base+output),4);assert(value==0xcafe0000+thread_slot);
 return count;
}
void curl_threads_body_reset(int fail){body_length=0;fail_write=fail;}
void curl_threads_body_check(unsigned expected){assert(body_length==expected);for(unsigned i=0;i<body_length;i++)assert(body[i]==(unsigned char)(i*37+11));}
void curl_threads_finish(unsigned *a,unsigned *f){assert(!blocks&&!live&&allocations==frees);for(unsigned i=0;i<5;i++)assert(callbacks[i]);for(unsigned i=1;i<5;i++)assert(timers[i]);*a=allocations;*f=frees;}
#define IMPORT(n) extern void tbimp_##n(void)
IMPORT(ThreadMalloc);IMPORT(ThreadFree);IMPORT(ThreadRealloc);IMPORT(ThreadStrdup);IMPORT(ThreadCalloc);IMPORT(ThreadTimer);IMPORT(ThreadWrite);IMPORT(curl_easy_perform);
IMPORT(curl_global_init_mem);IMPORT(curl_global_cleanup);IMPORT(curl_easy_init);IMPORT(curl_easy_cleanup);IMPORT(curl_easy_setopt);IMPORT(curl_easy_getinfo);IMPORT(curl_multi_init);IMPORT(curl_multi_cleanup);IMPORT(curl_multi_setopt);IMPORT(curl_multi_add_handle);IMPORT(curl_multi_remove_handle);IMPORT(curl_multi_socket_action);
extern void tribes_return_to_host(void);
void *tribes_lookup(uint32_t p){switch(p){
#define CASE(v,n) case v:return tbimp_##n
 CASE(1,ThreadMalloc);CASE(2,ThreadFree);CASE(3,ThreadRealloc);CASE(4,ThreadStrdup);CASE(5,ThreadCalloc);CASE(6,ThreadTimer);CASE(7,ThreadWrite);CASE(22,curl_easy_perform);
 CASE(10,curl_global_init_mem);CASE(11,curl_global_cleanup);CASE(12,curl_easy_init);CASE(13,curl_easy_cleanup);CASE(14,curl_easy_setopt);CASE(15,curl_easy_getinfo);CASE(16,curl_multi_init);CASE(17,curl_multi_cleanup);CASE(18,curl_multi_setopt);CASE(19,curl_multi_add_handle);CASE(20,curl_multi_remove_handle);CASE(21,curl_multi_socket_action);
 case 0xfffff000:return tribes_return_to_host;default:abort();}}
_Noreturn void tribes_unimplemented_dinput(_cpu *c,uint32_t index){(void)c;(void)index;abort();}
