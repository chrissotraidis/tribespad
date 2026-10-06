#include "../port/core/guest_tls.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t slot;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static unsigned stage;
static void *reuse(void *unused) {
    (void)unused;assert(tribes_tls_get(slot)==0);assert(tribes_tls_set(slot,0xbbbb));
    pthread_mutex_lock(&gate);stage=1;pthread_cond_signal(&condition);
    while(stage<2)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);
    assert(tribes_tls_get(slot)==0);assert(tribes_tls_set(slot,0xcccc));assert(tribes_tls_get(slot)==0xcccc);return NULL;
}
static void *isolation(void *argument) {
    uint32_t id=(uint32_t)(uintptr_t)argument;assert(tribes_tls_get(slot)==0);
    for(unsigned i=0;i<15000;i++) {
        uint32_t value=(id<<24)|i;tribes_set_last_error(333);
        assert(tribes_tls_set(slot,value)&&tribes_get_last_error()==333);
        assert(tribes_tls_get(slot)==value&&tribes_get_last_error()==0);
    }
    return NULL;
}
int main(void) {
    tribes_set_last_error(123);slot=tribes_tls_alloc();assert(slot!=UINT32_MAX&&tribes_get_last_error()==123);
    assert(tribes_tls_get(slot)==0&&tribes_get_last_error()==0);assert(tribes_tls_set(slot,0xaaaa));
    pthread_t thread;assert(!pthread_create(&thread,NULL,reuse,NULL));
    pthread_mutex_lock(&gate);while(stage<1)pthread_cond_wait(&condition,&gate);
    uint32_t old=slot;assert(tribes_tls_free(old));slot=tribes_tls_alloc();assert(slot==old&&tribes_tls_get(slot)==0);
    stage=2;pthread_cond_signal(&condition);pthread_mutex_unlock(&gate);assert(!pthread_join(thread,NULL));assert(tribes_tls_get(slot)==0);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,isolation,(void *)(uintptr_t)(i+1)));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(tribes_tls_free(slot));
    uint32_t indexes[1088];unsigned char seen[1088]={0};
    for(unsigned i=0;i<1088;i++){indexes[i]=tribes_tls_alloc();assert(indexes[i]<1088&&!seen[indexes[i]]);seen[indexes[i]]=1;}
    assert(tribes_tls_alloc()==UINT32_MAX&&tribes_get_last_error()==8);
    for(unsigned i=0;i<1088;i++)assert(tribes_tls_free(indexes[i]));
    assert(!tribes_tls_get(UINT32_MAX)&&tribes_get_last_error()==87);assert(!tribes_tls_set(UINT32_MAX,1));assert(!tribes_tls_free(UINT32_MAX));
    puts("PASS: thread-local zero/value/last-error semantics, live-thread slot reuse, 120000 isolated set/get cycles, 1088-slot exhaustion/recovery");return 0;
}
