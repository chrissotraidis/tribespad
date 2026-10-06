#include "guest_once.h"
#include "guest_heap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Once {uint64_t base;uint32_t address,word,context;unsigned state,waiters;pthread_t owner;pthread_cond_t changed;struct Once *next;} Once;
static Once *objects;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static _Noreturn void unsupported(const char *why){fprintf(stderr,"TribesPad: once initialization unsupported: %s\n",why);abort();}
static int valid(uint64_t base,uint32_t address){return address>=0x10000&&address<=UINT32_MAX-3&&base<=UINTPTR_MAX-address-3;}
static Once *find(uint64_t base,uint32_t address){for(Once *o=objects;o;o=o->next)if(o->base==base&&o->address==address)return o;return NULL;}
static void check(Once *o){uint32_t word;memcpy(&word,(void *)(uintptr_t)(o->base+o->address),4);if(word!=o->word)unsupported("modified live INIT_ONCE word");}
static void publish(Once *o,uint32_t word){o->word=word;memcpy((void *)(uintptr_t)(o->base+o->address),&word,4);}
uint32_t tribes_once_begin(uint64_t base,uint32_t address,uint32_t *context){
    if(!valid(base,address)||!context){tribes_set_last_error(87);return 0;}
    pthread_mutex_lock(&lock);Once *o=find(base,address);
    if(!o){uint32_t word;memcpy(&word,(void *)(uintptr_t)(base+address),4);if(word)unsupported("unowned nonzero INIT_ONCE");o=calloc(1,sizeof *o);if(!o){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}o->base=base;o->address=address;if(pthread_cond_init(&o->changed,NULL))unsupported("native condition initialization");o->next=objects;objects=o;}
    check(o);
    while(o->state==1){
        if(pthread_equal(o->owner,pthread_self()))unsupported("recursive initialization of same object");
        o->waiters++;int error=pthread_cond_wait(&o->changed,&lock);o->waiters--;if(error)unsupported("native condition wait");check(o);
    }
    if(o->state==2){*context=o->context;pthread_mutex_unlock(&lock);return 2;}
    o->state=1;o->owner=pthread_self();publish(o,1);*context=0;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_once_complete(uint64_t base,uint32_t address,uint32_t success,uint32_t context){
    if(!valid(base,address))unsupported("invalid completion address");
    if(success&&(context&3))unsupported("context uses reserved low two bits");
    pthread_mutex_lock(&lock);Once *o=find(base,address);
    if(!o||o->state!=1||!pthread_equal(o->owner,pthread_self()))unsupported("completion without thread ownership");check(o);
    o->state=success?2:0;o->context=success?context:0;publish(o,success?(context|2):0);
    if(pthread_cond_broadcast(&o->changed))unsupported("native completion wake");pthread_mutex_unlock(&lock);return success?1:0;
}
void tribes_once_initialize(uint64_t base,uint32_t address){
    if(!valid(base,address))unsupported("invalid initialization address");
    pthread_mutex_lock(&lock);Once *o=find(base,address);
    if(o){check(o);if(o->state==1||o->waiters)unsupported("reinitialize live initialization");o->state=0;o->context=0;publish(o,0);}
    else{uint32_t zero=0;memcpy((void *)(uintptr_t)(base+address),&zero,4);}
    pthread_mutex_unlock(&lock);
}
