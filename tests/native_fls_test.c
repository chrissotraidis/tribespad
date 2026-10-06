#include "../port/core/native_fls.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static const uint64_t base=0x12340000;
static pthread_mutex_t events_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned events,disposed;static uint32_t values[10000];
static uint32_t slot,other;
static void cleanup(void *ctx,uint32_t callback,uint32_t value) {
    assert(ctx==(void *)1&&callback==0x550000&&value);
    /* Reenter the registry: never executed with its mutex held. */
    (void)tribes_fls_get_value(base,other);
    uint32_t temporary=tribes_fls_alloc_index(base,0);assert(temporary!=UINT32_MAX);
    assert(tribes_fls_free_index(base,temporary,NULL,NULL));
    pthread_mutex_lock(&events_lock);assert(events<10000);values[events++]=value;pthread_mutex_unlock(&events_lock);
}
static void dispose(void *ctx){assert(ctx==(void *)1);pthread_mutex_lock(&events_lock);disposed++;pthread_mutex_unlock(&events_lock);}
static void *exit_worker(void *arg) {
    uint32_t id=(uint32_t)(uintptr_t)arg;assert(tribes_fls_fiber_create(base,cleanup,(void *)1,dispose));
    assert(!tribes_fls_get_value(base,slot));assert(tribes_fls_set_value(base,slot,id));
    for(unsigned i=0;i<1000;i++){tribes_set_last_error(123);assert(tribes_fls_set_value(base,other,id)&&tribes_get_last_error()==123);assert(tribes_fls_get_value(base,other)==id&&tribes_get_last_error()==0);}
    return NULL; /* pthread key destructor must run the actual cleanup. */
}
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static unsigned ready,release;
static void *free_worker(void *arg) {
    assert(tribes_fls_fiber_create(base,cleanup,(void *)1,dispose));assert(tribes_fls_set_value(base,slot,(uint32_t)(uintptr_t)arg));
    pthread_mutex_lock(&gate);ready++;pthread_cond_broadcast(&condition);while(!release)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);
    assert(!tribes_fls_get_value(base,slot));return NULL;
}
int main(void) {
    tribes_set_last_error(123);slot=tribes_fls_alloc_index(base,0x550000);other=tribes_fls_alloc_index(base,0);assert(slot==0&&other==1&&tribes_get_last_error()==123);
    TribesFlsFiber *a=tribes_fls_fiber_create(base,cleanup,(void *)1,dispose);assert(a);assert(tribes_fls_set_value(base,slot,10));
    TribesFlsFiber *b=tribes_fls_fiber_create(base,cleanup,(void *)1,dispose);assert(b);assert(tribes_fls_fiber_select(b));assert(!tribes_fls_get_value(base,slot));assert(tribes_fls_set_value(base,slot,20));assert(tribes_fls_fiber_select(a));assert(tribes_fls_get_value(base,slot)==10);
    assert(tribes_fls_free_index(base,slot,cleanup,(void *)1));assert(events==2);uint32_t reused=tribes_fls_alloc_index(base,0x550000);assert(reused==slot);assert(!tribes_fls_get_value(base,slot));assert(tribes_fls_fiber_select(b));assert(!tribes_fls_get_value(base,slot));
    assert(tribes_fls_set_value(base,slot,30));assert(tribes_fls_fiber_delete(b));assert(events==3&&disposed==1);assert(tribes_fls_current_context(base)==(void *)1);
    assert(tribes_fls_set_value(base,slot,0));pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,exit_worker,(void *)(uintptr_t)(100+i)));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(events==11&&disposed==9);
    for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,free_worker,(void *)(uintptr_t)(200+i)));
    pthread_mutex_lock(&gate);while(ready<8)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);
    assert(tribes_fls_free_index(base,slot,cleanup,(void *)1));assert(events==19);slot=tribes_fls_alloc_index(base,0x550000);assert(slot==0);
    pthread_mutex_lock(&gate);release=1;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&gate);for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(events==19&&disposed==17);
    assert(tribes_fls_free_index(base,slot,cleanup,(void *)1));assert(tribes_fls_free_index(base,other,NULL,NULL));
    uint32_t indexes[TRIBES_FLS_CAPACITY];for(unsigned i=0;i<TRIBES_FLS_CAPACITY;i++){indexes[i]=tribes_fls_alloc_index(base,0);assert(indexes[i]==i);}assert(tribes_fls_alloc_index(base,0)==UINT32_MAX&&tribes_get_last_error()==8);for(unsigned i=0;i<TRIBES_FLS_CAPACITY;i++)assert(tribes_fls_free_index(base,indexes[i],NULL,NULL));
    assert(!tribes_fls_get_value(base,UINT32_MAX)&&tribes_get_last_error()==87);assert(!tribes_fls_set_value(base,UINT32_MAX,1));assert(!tribes_fls_free_index(base,UINT32_MAX,NULL,NULL));
    assert(tribes_fls_alloc_index(base+1,0)==0);assert(tribes_fls_alloc_index(base,0)==0);assert(tribes_fls_free_index(base+1,0,NULL,NULL));assert(tribes_fls_free_index(base,0,NULL,NULL));assert(tribes_fls_fiber_delete(a));assert(disposed==18);
    puts("PASS: real FLS callback dispatch on index release/fiber delete/native thread exit, reentrancy, two-fiber isolation, live-thread reuse, process namespaces, 8000 concurrent set/get cycles, 128-slot exhaustion");return 0;
}
