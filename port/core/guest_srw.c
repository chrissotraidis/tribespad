/* Four-byte x86 SRW objects over native mutex/condition ownership. Guest words
   are opaque; idle objects return to all-zero state and leave no native record.
   No fairness/FIFO, recursive shared use, upgrade, cancellation,
   or Windows internal bit-encoding parity is claimed. Invalid lifetime or
   ownership stops explicitly rather than inventing success or guest SEH. */
#include "guest_srw.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
typedef struct Reader { pthread_t owner;struct Reader *next; } Reader;
typedef struct Lock { uintptr_t key;pthread_cond_t changed;pthread_t owner;int writer;unsigned waiters;Reader *readers;struct Lock *next; } Lock;
static Lock *locks;
static pthread_mutex_t catalog=PTHREAD_MUTEX_INITIALIZER;
static _Noreturn void stop(const char *reason,uint32_t guest) { fprintf(stderr,"TribesPad: SRW %s at 0x%08x\n",reason,guest);abort(); }
static uintptr_t key_for(uint64_t base,uint32_t guest) { if(!guest||guest>UINT32_MAX-3||base>UINTPTR_MAX-guest-3)stop("invalid address",guest);return (uintptr_t)(base+guest); }
static void check(int error,uint32_t guest) { if(error)stop("native synchronization failure",guest); }
static Lock *find(uintptr_t key) { for(Lock *s=locks;s;s=s->next)if(s->key==key)return s;return NULL; }
static uint32_t state(Lock *s) { return (s->writer?1u:0u)|(s->readers?2u:0u)|(s->waiters?4u:0u); }
static void publish(Lock *s) { uint32_t word=state(s);memcpy((void *)s->key,&word,4); }
static void validate(Lock *s,uint32_t guest) { uint32_t word;memcpy(&word,(void *)s->key,4);if(word!=state(s))stop("object modified while live",guest); }
static Lock *get(uintptr_t key,uint32_t guest) {
    Lock *s=find(key);if(s){validate(s,guest);return s;}
    uint32_t word;memcpy(&word,(void *)key,4);if(word)stop("nonzero static / copied live object",guest);
    s=calloc(1,sizeof *s);if(!s)stop("native metadata allocation",guest);
    check(pthread_cond_init(&s->changed,NULL),guest);s->key=key;s->next=locks;locks=s;return s;
}
static int reader_owned(Lock *s,pthread_t owner) { for(Reader *r=s->readers;r;r=r->next)if(pthread_equal(r->owner,owner))return 1;return 0; }
static void forget_idle(Lock *s,uint32_t guest) {
    publish(s);if(s->writer||s->readers||s->waiters)return;
    Lock **link=&locks;while(*link!=s)link=&(*link)->next;*link=s->next;
    check(pthread_cond_destroy(&s->changed),guest);free(s);
}
void tribes_srw_initialize(uint64_t base,uint32_t guest) {
    uintptr_t key=key_for(base,guest);check(pthread_mutex_lock(&catalog),guest);
    if(find(key))stop("initialize busy object",guest);
    uint32_t word=0;memcpy((void *)key,&word,4);check(pthread_mutex_unlock(&catalog),guest);
}
static uint32_t acquire(uint64_t base,uint32_t guest,int exclusive,int attempt) {
    uintptr_t key=key_for(base,guest);pthread_t self=pthread_self();check(pthread_mutex_lock(&catalog),guest);Lock *s=get(key,guest);
    int own_writer=s->writer&&pthread_equal(s->owner,self),own_reader=reader_owned(s,self);
    if(!exclusive&&own_reader)stop("recursive shared acquisition unsupported",guest);
    if((own_writer||own_reader)&&!attempt)stop("recursive acquisition / upgrade unsupported",guest);
    if(attempt&&(s->writer||(exclusive&&s->readers))) { check(pthread_mutex_unlock(&catalog),guest);return 0; }
    if(!attempt) {
        if(s->waiters==UINT_MAX)stop("waiter overflow",guest);
        ++s->waiters;publish(s);
        while(s->writer||(exclusive&&s->readers))check(pthread_cond_wait(&s->changed,&catalog),guest);
        --s->waiters;
    }
    if(exclusive) { s->writer=1;s->owner=self; }
    else { Reader *r=malloc(sizeof *r);if(!r)stop("reader metadata allocation",guest);r->owner=self;r->next=s->readers;s->readers=r; }
    publish(s);check(pthread_mutex_unlock(&catalog),guest);return 1;
}
void tribes_srw_acquire_exclusive(uint64_t base,uint32_t guest) { (void)acquire(base,guest,1,0); }
void tribes_srw_acquire_shared(uint64_t base,uint32_t guest) { (void)acquire(base,guest,0,0); }
uint32_t tribes_srw_try_exclusive(uint64_t base,uint32_t guest) { return acquire(base,guest,1,1); }
uint32_t tribes_srw_try_shared(uint64_t base,uint32_t guest) { return acquire(base,guest,0,1); }
static void release(uint64_t base,uint32_t guest,int exclusive) {
    uintptr_t key=key_for(base,guest);pthread_t self=pthread_self();check(pthread_mutex_lock(&catalog),guest);Lock *s=find(key);
    if(!s)stop("release unowned object",guest);validate(s,guest);
    if(exclusive) { if(!s->writer||!pthread_equal(s->owner,self))stop("exclusive release ownership",guest);s->writer=0; }
    else {
        Reader **link=&s->readers;while(*link&&!pthread_equal((*link)->owner,self))link=&(*link)->next;
        if(!*link)stop("shared release ownership",guest);Reader *r=*link;*link=r->next;free(r);
    }
    check(pthread_cond_broadcast(&s->changed),guest);forget_idle(s,guest);check(pthread_mutex_unlock(&catalog),guest);
}
void tribes_srw_release_exclusive(uint64_t base,uint32_t guest) { release(base,guest,1); }
void tribes_srw_release_shared(uint64_t base,uint32_t guest) { release(base,guest,0); }
uint32_t tribes_srw_live_count(void) { check(pthread_mutex_lock(&catalog),0);uint32_t n=0;for(Lock *s=locks;s;s=s->next)++n;check(pthread_mutex_unlock(&catalog),0);return n; }
uint32_t tribes_srw_waiters(uint64_t base,uint32_t guest) { uintptr_t key=key_for(base,guest);check(pthread_mutex_lock(&catalog),guest);Lock *s=find(key);uint32_t n=s?s->waiters:0;check(pthread_mutex_unlock(&catalog),guest);return n; }
