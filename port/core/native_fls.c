/* Owned FLS indices and per-fiber values, with native thread-exit cleanup.
   Callbacks are dispatched outside the registry mutex. A retiring index cannot
   be reused until its snapshot callbacks finish. No cleanup callback is skipped.
   https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsfree
   https://learn.microsoft.com/en-us/windows/win32/api/winnt/nc-winnt-pfls_callback_function */
#include "native_fls.h"
#include "guest_heap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct Process {
    uint64_t base;
    struct { unsigned state;uint32_t callback; } slots[TRIBES_FLS_CAPACITY];
    struct Process *next;
} Process;
struct TribesFlsFiber {
    Process *process;pthread_t owner;unsigned selected,draining;
    uint32_t values[TRIBES_FLS_CAPACITY];
    TribesFlsDispatch dispatch;TribesFlsDispose dispose;void *context;
    struct TribesFlsFiber *global_next,*thread_next;
};
typedef struct Job { uint32_t callback,value; } Job;
static Process *processes;
static TribesFlsFiber *fibers;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t key;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static int key_error;
static void thread_exit(void *head);
static void init_key(void){key_error=pthread_key_create(&key,thread_exit);}
static Process *find_process(uint64_t base,int create) {
    for(Process *p=processes;p;p=p->next)if(p->base==base)return p;
    if(!create)return NULL;
    Process *p=calloc(1,sizeof *p);if(p){p->base=base;p->next=processes;processes=p;}return p;
}
static TribesFlsFiber *current(uint64_t base) {
    if(pthread_once(&once,init_key)||key_error)return NULL;
    for(TribesFlsFiber *f=pthread_getspecific(key);f;f=f->thread_next)if(f->process->base==base&&f->selected)return f;
    return NULL;
}
static void invoke(TribesFlsDispatch dispatch,void *context,Job job) {
    if(!dispatch){fprintf(stderr,"TribesPad: FLS cleanup requires guest callback dispatch\n");abort();}
    dispatch(context,job.callback,job.value);
}
TribesFlsFiber *tribes_fls_fiber_create(uint64_t base,TribesFlsDispatch dispatch,void *context,TribesFlsDispose dispose) {
    if(pthread_once(&once,init_key)||key_error){tribes_set_last_error(8);return NULL;}
    TribesFlsFiber *f=calloc(1,sizeof *f);if(!f){tribes_set_last_error(8);return NULL;}
    f->owner=pthread_self();f->dispatch=dispatch;f->context=context;f->dispose=dispose;
    f->selected=current(base)==NULL;f->thread_next=pthread_getspecific(key);
    pthread_mutex_lock(&lock);f->process=find_process(base,1);
    if(!f->process||pthread_setspecific(key,f)){pthread_mutex_unlock(&lock);free(f);tribes_set_last_error(8);return NULL;}
    f->global_next=fibers;fibers=f;pthread_mutex_unlock(&lock);return f;
}
int tribes_fls_fiber_select(TribesFlsFiber *fiber) {
    if(!fiber){tribes_set_last_error(87);return 0;}
    TribesFlsFiber *head=pthread_getspecific(key),*found=NULL;
    for(TribesFlsFiber *f=head;f;f=f->thread_next)if(f==fiber)found=f;
    if(!found||found->draining){tribes_set_last_error(87);return 0;}
    for(TribesFlsFiber *f=head;f;f=f->thread_next)if(f->process==found->process)f->selected=f==found;
    return 1;
}
void *tribes_fls_current_context(uint64_t base){TribesFlsFiber *f=current(base);return f?f->context:NULL;}
uint32_t tribes_fls_alloc_index(uint64_t base,uint32_t callback) {
    pthread_mutex_lock(&lock);Process *p=find_process(base,1);
    if(p)for(uint32_t i=0;i<TRIBES_FLS_CAPACITY;i++)if(!p->slots[i].state){p->slots[i].state=1;p->slots[i].callback=callback;pthread_mutex_unlock(&lock);return i;}
    pthread_mutex_unlock(&lock);tribes_set_last_error(8);return UINT32_MAX;
}
uint32_t tribes_fls_get_value(uint64_t base,uint32_t index) {
    TribesFlsFiber *f=current(base);pthread_mutex_lock(&lock);Process *p=find_process(base,0);
    int valid=p&&index<TRIBES_FLS_CAPACITY&&p->slots[index].state==1;uint32_t value=valid&&f?f->values[index]:0;
    pthread_mutex_unlock(&lock);tribes_set_last_error(valid?0:87);return value;
}
uint32_t tribes_fls_set_value(uint64_t base,uint32_t index,uint32_t value) {
    TribesFlsFiber *f=current(base);pthread_mutex_lock(&lock);Process *p=find_process(base,0);
    if(!p||index>=TRIBES_FLS_CAPACITY||p->slots[index].state!=1){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
    if(!f){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
    if(f->draining&&value){fprintf(stderr,"TribesPad: FLS repopulation during fiber teardown is unimplemented\n");abort();}
    f->values[index]=value;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_fls_free_index(uint64_t base,uint32_t index,TribesFlsDispatch dispatch,void *context) {
    pthread_mutex_lock(&lock);Process *p=find_process(base,0);
    if(!p||index>=TRIBES_FLS_CAPACITY||p->slots[index].state!=1){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
    size_t count=0;uint32_t callback=p->slots[index].callback;
    for(TribesFlsFiber *f=fibers;f;f=f->global_next)if(f->process==p&&f->values[index]&&callback)count++;
    Job *jobs=count?calloc(count,sizeof *jobs):NULL;
    if(count&&!jobs){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
    p->slots[index].state=2;size_t n=0;
    for(TribesFlsFiber *f=fibers;f;f=f->global_next)if(f->process==p){if(f->values[index]&&callback)jobs[n++]=(Job){callback,f->values[index]};f->values[index]=0;}
    pthread_mutex_unlock(&lock);
    for(size_t i=0;i<n;i++)invoke(dispatch,context,jobs[i]);free(jobs);
    pthread_mutex_lock(&lock);p->slots[index].callback=0;p->slots[index].state=0;pthread_mutex_unlock(&lock);return 1;
}
int tribes_fls_fiber_delete(TribesFlsFiber *fiber) {
    if(!fiber){tribes_set_last_error(87);return 0;}
    TribesFlsFiber *head=pthread_getspecific(key),**link=&head;
    while(*link&&*link!=fiber)link=&(*link)->thread_next;
    if(!*link||fiber->draining){tribes_set_last_error(87);return 0;}
    Job jobs[TRIBES_FLS_CAPACITY];size_t n=0;unsigned was_selected=fiber->selected;
    /* Make this storage context current while its cleanup callback runs. */
    TribesFlsFiber *previous=current(fiber->process->base);
    if(previous)previous->selected=0;fiber->selected=1;fiber->draining=1;
    pthread_mutex_lock(&lock);
    for(unsigned i=0;i<TRIBES_FLS_CAPACITY;i++){
        if(fiber->values[i]&&fiber->process->slots[i].state==1&&fiber->process->slots[i].callback)jobs[n++]=(Job){fiber->process->slots[i].callback,fiber->values[i]};
        fiber->values[i]=0;
    }
    pthread_mutex_unlock(&lock);
    for(size_t i=0;i<n;i++)invoke(fiber->dispatch,fiber->context,jobs[i]);
    pthread_mutex_lock(&lock);TribesFlsFiber **global=&fibers;while(*global!=fiber)global=&(*global)->global_next;*global=fiber->global_next;pthread_mutex_unlock(&lock);
    /* Callback reentrancy can add other fibers, so find links afresh. */
    head=pthread_getspecific(key);link=&head;while(*link!=fiber)link=&(*link)->thread_next;*link=fiber->thread_next;
    if(pthread_setspecific(key,head))abort();
    int restored=0;
    if(previous&&previous!=fiber)for(TribesFlsFiber *f=head;f;f=f->thread_next)if(f==previous){f->selected=1;restored=1;break;}
    if(!restored&&was_selected)for(TribesFlsFiber *f=head;f;f=f->thread_next)if(f->process==fiber->process){f->selected=1;break;}
    if(fiber->dispose)fiber->dispose(fiber->context);free(fiber);return 1;
}
static void thread_exit(void *head) {
    if(pthread_setspecific(key,head))abort();
    while((head=pthread_getspecific(key))!=NULL)if(!tribes_fls_fiber_delete(head))abort();
}

void tribes_fls_thread_cleanup(uint64_t base) {
    if(pthread_once(&once,init_key)||key_error)abort();
    for(;;){TribesFlsFiber *f=pthread_getspecific(key);while(f&&f->process->base!=base)f=f->thread_next;if(!f)break;if(!tribes_fls_fiber_delete(f))abort();}
}
