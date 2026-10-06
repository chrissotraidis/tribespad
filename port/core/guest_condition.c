/* Windows condition variables over Apple pthread waits. Guest objects are
   opaque x86 four-byte words, zero when idle; host pointers never escape.
   See config/runtime/condition-variables.json for contracts and limitations. */
#include "guest_condition.h"
#include "guest_srw.h"
#include "critical_section.h"
#include "guest_heap.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct Waiter { pthread_cond_t changed;int selected;struct Waiter *next; } Waiter;
typedef struct Condition { uintptr_t key;Waiter *waiters;struct Condition *next; } Condition;
static Condition *conditions;
static pthread_mutex_t catalog=PTHREAD_MUTEX_INITIALIZER;
static _Noreturn void stop(const char *reason,uint32_t guest) {
    fprintf(stderr,"TribesPad: condition variable %s at 0x%08x\n",reason,guest);abort();
}
static void check(int error,uint32_t guest) { if(error)stop("native synchronization failure",guest); }
static uintptr_t key_for(uint64_t base,uint32_t guest) {
    if(!guest||guest>UINT32_MAX-3||base>UINTPTR_MAX-guest-3)stop("invalid address",guest);
    return (uintptr_t)(base+guest);
}
static Condition *find(uintptr_t key) { for(Condition *c=conditions;c;c=c->next)if(c->key==key)return c;return NULL; }
static Condition *validate(uintptr_t key,uint32_t guest) {
    Condition *c=find(key);uint32_t word;memcpy(&word,(void *)key,4);
    if(word!=(c?1u:0u))stop("modified / copied live object",guest);
    return c;
}
static void publish(uintptr_t key,uint32_t value) { memcpy((void *)key,&value,4); }
void tribes_condition_initialize(uint64_t base,uint32_t guest) {
    uintptr_t key=key_for(base,guest);check(pthread_mutex_lock(&catalog),guest);
    if(find(key))stop("initialize busy object",guest);
    publish(key,0);check(pthread_mutex_unlock(&catalog),guest);
}
static void wake(uint64_t base,uint32_t guest,int all) {
    uintptr_t key=key_for(base,guest);check(pthread_mutex_lock(&catalog),guest);
    Condition *c=validate(key,guest);
    if(c)for(Waiter *w=c->waiters;w;w=w->next)if(!w->selected){
        w->selected=1;check(pthread_cond_signal(&w->changed),guest);if(!all)break;
    }
    check(pthread_mutex_unlock(&catalog),guest);
}
void tribes_condition_wake(uint64_t base,uint32_t guest) { wake(base,guest,0); }
void tribes_condition_wake_all(uint64_t base,uint32_t guest) { wake(base,guest,1); }
static uint32_t sleep_on(uint64_t base,uint32_t guest,uint32_t lock,uint32_t ms,int mode) {
    uintptr_t key=key_for(base,guest);check(pthread_mutex_lock(&catalog),guest);
    Condition *c=validate(key,guest);
    if(!c){c=calloc(1,sizeof *c);if(!c)stop("metadata allocation",guest);c->key=key;c->next=conditions;conditions=c;}
    Waiter w={0};check(pthread_cond_init(&w.changed,NULL),guest);w.next=c->waiters;c->waiters=&w;publish(key,1);
    /* Wake takes this catalog too. Publishing, releasing the associated lock,
       and entering pthread_cond_wait cannot lose an intervening Wake. */
    if(mode==2)tribes_critical_condition_release(base,lock);
    else if(mode==1)tribes_srw_release_shared(base,lock);
    else tribes_srw_release_exclusive(base,lock);
    int error=ETIMEDOUT;
    if(ms==UINT32_MAX)error=pthread_cond_wait(&w.changed,&catalog);
    else if(ms){
        struct timespec relative={(time_t)(ms/1000),(long)(ms%1000)*1000000};
        error=pthread_cond_timedwait_relative_np(&w.changed,&catalog,&relative);
    }
    if(error&&error!=ETIMEDOUT)check(error,guest);
    /* A selected wake wins a native timeout race. Native spurious wakes are
       permitted by Windows; one wait avoids extending the requested timeout. */
    uint32_t success=!error||w.selected;
    (void)validate(key,guest);
    Waiter **link=&c->waiters;while(*link!=&w)link=&(*link)->next;*link=w.next;
    check(pthread_cond_destroy(&w.changed),guest);
    if(!c->waiters){Condition **p=&conditions;while(*p!=c)p=&(*p)->next;*p=c->next;publish(key,0);free(c);}
    check(pthread_mutex_unlock(&catalog),guest);
    /* Never retain the CV catalog while reacquiring a contended guest lock. */
    if(mode==2)tribes_critical_condition_reacquire(base,lock);
    else if(mode==1)tribes_srw_acquire_shared(base,lock);
    else tribes_srw_acquire_exclusive(base,lock);
    if(!success)tribes_set_last_error(1460);
    return success;
}
uint32_t tribes_condition_sleep_srw(uint64_t base,uint32_t guest,uint32_t lock,uint32_t ms,uint32_t flags) {
    if(flags>1)stop("unsupported SRW flags",guest);
    return sleep_on(base,guest,lock,ms,(int)flags);
}
uint32_t tribes_condition_sleep_cs(uint64_t base,uint32_t guest,uint32_t lock,uint32_t ms) { return sleep_on(base,guest,lock,ms,2); }
uint32_t tribes_condition_waiters(uint64_t base,uint32_t guest) {
    uintptr_t key=key_for(base,guest);check(pthread_mutex_lock(&catalog),guest);Condition *c=find(key);uint32_t n=0;
    if(c)for(Waiter *w=c->waiters;w;w=w->next)++n;
    check(pthread_mutex_unlock(&catalog),guest);return n;
}
uint32_t tribes_condition_live_count(void) {
    check(pthread_mutex_lock(&catalog),0);uint32_t n=0;for(Condition *c=conditions;c;c=c->next)++n;
    check(pthread_mutex_unlock(&catalog),0);return n;
}
