/* Starsiege's critical sections: TribesPad's Win32 critical sections
 * (port/core/critical_section.c) with re-entry counted here, as Windows does
 * (RecursionCount is a 32-bit field). Starsiege re-enters one section once per
 * frame; a recursive pthread mutex stops at macOS's recursion limit (EAGAIN)
 * after about a minute of play. The pthread mutex is taken once per owner.
 * Same API and checks otherwise; the Starsiege build compiles this file in
 * place of critical_section.c, whose reviewed hash stays unchanged. */
/* (Original header follows.) Win32 critical sections over native recursive pthread mutexes.
   Guest CRITICAL_SECTIONs are logically opaque, 24-byte x86 objects.
   Contract: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-entercriticalsection
   Undefined lifetime/ownership use fails closed; SEH exception delivery is pending. */
#include "../core/critical_section.h"
#include "../core/guest_heap.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct Section {
    uintptr_t key;
    pthread_mutex_t mutex;
    pthread_t owner;
    unsigned depth, references;
    uint32_t spin;
    struct Section *next;
} Section;
static Section *sections;
static pthread_mutex_t catalog = PTHREAD_MUTEX_INITIALIZER;
static _Noreturn void stop(const char *operation,uint32_t guest,int error) {
    fprintf(stderr,"TribesPad: critical-section %s at 0x%08x failed (%d)\n",operation,guest,error);
    abort();
}
static uintptr_t key_for(uint64_t base,uint32_t guest) {
    if (!guest || guest>UINT32_MAX-23 || base>UINTPTR_MAX-guest-23) stop("address",guest,EINVAL);
    return (uintptr_t)(base+guest);
}
static Section *find(uintptr_t key) {
    for(Section *s=sections;s;s=s->next)if(s->key==key)return s;
    return NULL;
}
static uint32_t initialize(uint64_t base,uint32_t guest,uint32_t spin,int fallible) {
    uintptr_t key=key_for(base,guest);
    pthread_mutex_lock(&catalog);
    if(find(key))stop("reinitialize",guest,EALREADY);
    Section *s=calloc(1,sizeof(*s));
    if(!s){if(!fallible)stop("allocate",guest,ENOMEM);pthread_mutex_unlock(&catalog);tribes_set_last_error(8);return 0;}
    pthread_mutexattr_t attr;int error=pthread_mutexattr_init(&attr);
    if(fallible&&(error==ENOMEM||error==EAGAIN)){free(s);pthread_mutex_unlock(&catalog);tribes_set_last_error(8);return 0;}
    if(error)stop("attribute",guest,error);
    error=pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_NORMAL);
    if(!error)error=pthread_mutex_init(&s->mutex,&attr);
    int cleanup=pthread_mutexattr_destroy(&attr);
    if(fallible&&!cleanup&&(error==ENOMEM||error==EAGAIN)){free(s);pthread_mutex_unlock(&catalog);tribes_set_last_error(8);return 0;}
    if(error||cleanup)stop("initialize",guest,error?error:cleanup);
    s->spin=sysconf(_SC_NPROCESSORS_ONLN)>1?spin:0;
    uint32_t opaque[6]={0,UINT32_MAX,0,0,0,s->spin};memcpy((void *)key,opaque,sizeof opaque);
    s->key=key;s->next=sections;sections=s;
    pthread_mutex_unlock(&catalog);return 1;
}
uint32_t tribes_critical_init(uint64_t base,uint32_t guest) { (void)initialize(base,guest,0,0);return 0; }
uint32_t tribes_critical_init_spin(uint64_t base,uint32_t guest,uint32_t spin) { return initialize(base,guest,spin,1); }
/* Selected QoL imports this Vista API directly. The compatibility OS identity
   does not imply that every later service is unavailable. Flags zero preserves
   the same native ownership/spin semantics; other modes remain explicit gates. */
uint32_t tribes_critical_init_ex(uint64_t base,uint32_t guest,uint32_t spin,uint32_t flags) {
    if(flags)stop("unsupported Ex flags",guest,EINVAL);
    return initialize(base,guest,spin,1);
}
static uint32_t acquire(uint64_t base,uint32_t guest,int attempt) {
    uintptr_t key=key_for(base,guest);
    pthread_mutex_lock(&catalog);Section *s=find(key);
    if(!s)stop("uninitialized",guest,EINVAL);
    if(s->references==UINT_MAX)stop("reference overflow",guest,EOVERFLOW);
    ++s->references;
    if(s->depth&&pthread_equal(s->owner,pthread_self())){
        if(s->depth==UINT_MAX)stop("recursion overflow",guest,EOVERFLOW);
        ++s->depth;pthread_mutex_unlock(&catalog);return 1;
    }
    pthread_mutex_unlock(&catalog);
    int error=EBUSY;
    if(attempt)error=pthread_mutex_trylock(&s->mutex);
    else {
        for(uint32_t i=0;i<s->spin&&error==EBUSY;i++)error=pthread_mutex_trylock(&s->mutex);
        if(error==EBUSY)error=pthread_mutex_lock(&s->mutex);
    }
    pthread_mutex_lock(&catalog);
    if(error) {
        --s->references;pthread_mutex_unlock(&catalog);
        if(attempt&&error==EBUSY)return 0;
        stop("lock",guest,error);
    }
    if(s->depth)stop("owner",guest,EPERM);
    s->owner=pthread_self();s->depth=1;pthread_mutex_unlock(&catalog);return 1;
}
uint32_t tribes_critical_enter(uint64_t base,uint32_t guest) { (void)acquire(base,guest,0);return 0; }
uint32_t tribes_critical_try_enter(uint64_t base,uint32_t guest) { return acquire(base,guest,1); }
uint32_t tribes_critical_leave(uint64_t base,uint32_t guest) {
    uintptr_t key=key_for(base,guest);
    pthread_mutex_lock(&catalog);Section *s=find(key);
    if(!s||!s->depth||!pthread_equal(s->owner,pthread_self()))stop("leave ownership",guest,EPERM);
    --s->depth;--s->references;
    if(!s->depth){int error=pthread_mutex_unlock(&s->mutex);if(error)stop("unlock",guest,error);}
    pthread_mutex_unlock(&catalog);return 0;
}
void tribes_critical_condition_release(uint64_t base,uint32_t guest) {
    uintptr_t key=key_for(base,guest);
    pthread_mutex_lock(&catalog);Section *s=find(key);
    if(!s||s->depth!=1||!pthread_equal(s->owner,pthread_self()))stop("condition wait requires single ownership",guest,EPERM);
    int error=pthread_mutex_unlock(&s->mutex);if(error)stop("condition unlock",guest,error);
    --s->depth; /* Keep its reference until reacquired, rejecting delete while asleep. */
    pthread_mutex_unlock(&catalog);
}
void tribes_critical_condition_reacquire(uint64_t base,uint32_t guest) {
    (void)acquire(base,guest,0);
    pthread_mutex_lock(&catalog);Section *s=find(key_for(base,guest));
    if(!s||s->references<2)stop("condition reference",guest,EINVAL);
    --s->references;pthread_mutex_unlock(&catalog);
}
void tribes_critical_condition_cancel(uint64_t base,uint32_t guest) {
    /* A cancelled condition wait already released ownership, but still holds
       the reference that normally survives until condition_reacquire. */
    pthread_mutex_lock(&catalog);Section *s=find(key_for(base,guest));
    if(!s||!s->references)stop("cancelled condition reference",guest,EINVAL);
    --s->references;pthread_mutex_unlock(&catalog);
}
uint32_t tribes_critical_delete(uint64_t base,uint32_t guest) {
    uintptr_t key=key_for(base,guest);
    pthread_mutex_lock(&catalog);Section **link=&sections;
    while(*link&&(*link)->key!=key)link=&(*link)->next;
    Section *s=*link;if(!s)stop("delete uninitialized",guest,EINVAL);
    if(s->references||s->depth)stop("delete busy",guest,EBUSY);
    int error=pthread_mutex_destroy(&s->mutex);if(error)stop("destroy",guest,error);
    *link=s->next;free(s);pthread_mutex_unlock(&catalog);return 0;
}
