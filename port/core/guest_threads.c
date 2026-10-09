/* Native pthread execution of finite AOT guest code. No interpreter/JIT.
   Guest stacks are fully committed, with a separate 64 KiB inaccessible lower
   guard reservation. Demand-grown stacks, suspension of running code, external
   termination, guest SEH and dynamic module loading remain explicit boundaries. */
#include "guest_threads.h"
#include "guest_callback.h"
#include "guest_virtual_memory.h"
#include "guest_teb.h"
#include "guest_heap.h"
#include "guest_modules.h"
#include "native_handles.h"
#include "native_fls.h"
#include "../llasm-support/tribes_absolute_memory.h"
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct ThreadModule {TribesThreadModule source;uint32_t offset;int active,disabled;struct ThreadModule *next;} ThreadModule;
typedef struct Process {uint64_t base;uint32_t pid,main_tid,reserve,environment_size;int ready;_Atomic int exiting;uint32_t exit_code;ThreadModule *modules;struct Process *next;} Process;
typedef struct Thread {
    Process *process;TribesThreadObject *object;_cpu cpu;jmp_buf escape;
    uint32_t stack,environment,stack_top,stack_bottom,start,parameter,id,code;
    _Atomic int phase;pthread_t native;struct Thread *next;
} Thread;
static Process *processes;
static Thread *threads;
static pthread_cond_t thread_finished=PTHREAD_COND_INITIALIZER;
static pthread_mutex_t registry=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t loader;
static pthread_cond_t initialized=PTHREAD_COND_INITIALIZER;
static pthread_once_t loader_once=PTHREAD_ONCE_INIT;
static _Thread_local Thread *self;
static uint32_t next_id=0x10000000,live_threads;
static _Noreturn void stop(const char *reason){fprintf(stderr,"TribesPad: guest thread %s\n",reason);abort();}
static void check(int error){if(error)stop("native synchronization failure");}
static void initialize_loader(void){pthread_mutexattr_t a;check(pthread_mutexattr_init(&a));check(pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE));check(pthread_mutex_init(&loader,&a));check(pthread_mutexattr_destroy(&a));}
void tribes_loader_lock(void){check(pthread_once(&loader_once,initialize_loader));check(pthread_mutex_lock(&loader));}
void tribes_loader_unlock(void){check(pthread_mutex_unlock(&loader));}
static Process *find(uint64_t base){for(Process *p=processes;p;p=p->next)if(p->base==base)return p;return NULL;}
static void free_modules(ThreadModule *m){while(m){ThreadModule *next=m->next;free((void *)m->source.template_bytes);free((void *)m->source.callbacks);free(m);m=next;}}
int tribes_threads_configure(uint64_t base,uint32_t pid,uint32_t main_tid,uint32_t reserve,const TribesThreadModule *modules,uint32_t count){
    if(!base||!pid||!main_tid||!reserve||reserve%65536||!modules||!count||count>64){errno=EINVAL;return 0;}
    Process *p=calloc(1,sizeof *p);if(!p)return 0;p->base=base;p->pid=pid;p->main_tid=main_tid;p->reserve=reserve;
    ThreadModule **tail=&p->modules;uint64_t end=8192,used=0;
    for(uint32_t i=0;i<count;i++){
        TribesThreadModule src=modules[i];
        if((src.dynamic&&(src.tls_index!=UINT32_MAX||src.callback_count||!src.entry))||!src.image||src.callback_count>128||(src.callback_count&&!src.callbacks)||(src.template_size&&!src.template_bytes)||
           (src.tls_index!=UINT32_MAX&&(src.tls_index>=64||(used&(UINT64_C(1)<<src.tls_index))))||
           (src.tls_index==UINT32_MAX&&(src.template_size||src.zero_fill))){errno=EINVAL;goto failure;}
        for(uint32_t j=0;j<i;j++)if(modules[j].image==src.image){errno=EINVAL;goto failure;}
        for(uint32_t j=0;j<src.callback_count;j++)if(!src.callbacks[j]){errno=EINVAL;goto failure;}
        if(src.tls_index!=UINT32_MAX)used|=UINT64_C(1)<<src.tls_index;
        uint64_t bytes=(uint64_t)src.template_size+src.zero_fill;
        if(bytes>UINT32_MAX||end+bytes>UINT32_MAX-65535){errno=EOVERFLOW;goto failure;}
        ThreadModule *m=calloc(1,sizeof *m);if(!m)goto failure;m->source=src;m->active=!src.dynamic;m->offset=(uint32_t)end;
        m->source.template_bytes=src.template_size?malloc(src.template_size):NULL;
        m->source.callbacks=src.callback_count?malloc(4ull*src.callback_count):NULL;
        if((src.template_size&&!m->source.template_bytes)||(src.callback_count&&!m->source.callbacks)){free_modules(m);goto failure;}
        if(src.template_size)memcpy((void *)m->source.template_bytes,src.template_bytes,src.template_size);
        if(src.callback_count)memcpy((void *)m->source.callbacks,src.callbacks,4ull*src.callback_count);
        *tail=m;tail=&m->next;end=(end+(bytes?bytes:1)+15)&~UINT64_C(15);
    }
    p->environment_size=(uint32_t)((end+65535)&~UINT64_C(65535));
    check(pthread_mutex_lock(&registry));if(find(base)){check(pthread_mutex_unlock(&registry));errno=EALREADY;goto failure;}
    uint32_t main_handle;TribesThreadObject *object=tribes_thread_object_create(main_tid,0,&main_handle);
    if(!object){check(pthread_mutex_unlock(&registry));errno=ENOMEM;goto failure;}
    tribes_thread_object_set_current(object);if(!tribes_close_handle(main_handle))stop("main object handle release");
    p->next=processes;processes=p;check(pthread_mutex_unlock(&registry));return 1;
failure:free_modules(p->modules);free(p);return 0;
}
void tribes_threads_process_ready(uint64_t base){
    check(pthread_mutex_lock(&registry));Process *p=find(base);check(pthread_mutex_unlock(&registry));if(!p)stop("unconfigured process ready");
    tribes_loader_lock();if(p->ready)stop("duplicate process ready");p->ready=1;check(pthread_cond_broadcast(&initialized));check(pthread_mutex_unlock(&loader));
}
/* Definitions are immutable after configure; activation shares the thread loader
   lock. Existing threads receive no retroactive attach, but do receive detach.
   https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain */
uint32_t tribes_threads_load_image(_cpu *cpu,uint32_t image){
    uint64_t base=cpu->_pointer_offset;tribes_loader_lock();
    check(pthread_mutex_lock(&registry));Process *p=find(base);check(pthread_mutex_unlock(&registry));
    ThreadModule *m=p?p->modules:NULL;while(m&&m->source.image!=image)m=m->next;
    if(!m||!m->source.dynamic||m->active)stop("unsupported dynamic image state/reload");
    m->active=2; /* visible to its own original entry; notifications await completion */
    if(!tribes_module_set_pending(base,image,0))stop("missing dynamic image metadata");
    uint32_t args[]={image,1,0};uint32_t result=tribes_call_guest(cpu,m->source.entry,3,args,cpu->_esp-64);
    fprintf(stderr,"Original AOT DLL attach image=0x%08x entry=0x%08x result=%u\n",image,m->source.entry,result);
    if(!result){args[1]=0;(void)tribes_call_guest(cpu,m->source.entry,3,args,cpu->_esp-64);m->active=-1;
        if(!tribes_module_set_pending(base,image,1))stop("failed DLL visibility rollback");
        tribes_set_last_error(1114);
    }else m->active=1;
    tribes_loader_unlock();return result?image:0;
}
uint32_t tribes_disable_thread_library_calls(uint64_t base,uint32_t image){
    tribes_loader_lock();check(pthread_mutex_lock(&registry));Process *p=find(base);check(pthread_mutex_unlock(&registry));
    ThreadModule *m=p?p->modules:NULL;while(m&&m->source.image!=image)m=m->next;
    int ok=m&&m->active&&m->source.tls_index==UINT32_MAX&&!m->source.callback_count;
    if(ok)m->disabled=1;else tribes_set_last_error(m?87:126);
    tribes_loader_unlock();return ok;
}
static void put(uint64_t base,uint32_t address,uint32_t value){memcpy((void *)(uintptr_t)(base+address),&value,4);}
static void notify_module(Thread *t,ThreadModule *m,uint32_t reason){
    if(m->active!=1||m->disabled)return;
    uint32_t args[]={m->source.image,reason,0};
    for(uint32_t i=0;i<m->source.callback_count;i++)(void)tribes_call_guest(&t->cpu,m->source.callbacks[i],3,args,t->stack_top-128);
    if(m->source.entry)(void)tribes_call_guest(&t->cpu,m->source.entry,3,args,t->stack_top-128);
}
static void detach_modules(Thread *t,ThreadModule *m){if(!m)return;detach_modules(t,m->next);notify_module(t,m,3);}
static void release_memory(Thread *t){
    if(t->environment&&!tribes_virtual_free(t->process->base,t->environment,0,0x8000))stop("environment release");
    if(t->stack&&!tribes_virtual_free(t->process->base,t->stack,0,0x8000))stop("stack release");
}
static void retire_thread(Thread *t,int terminated){
    uint64_t base=t->process->base;t->phase=3;t->cpu._esp=t->stack_top;
    if(terminated){tribes_guest_callback_reset();tribes_fls_thread_discard(base);}
    else {
        tribes_fls_thread_cleanup(base);tribes_guest_thread_finalize(&t->cpu);
        tribes_loader_lock();detach_modules(t,t->process->modules);tribes_loader_unlock();
        tribes_fls_thread_cleanup(base);tribes_guest_thread_finalize(&t->cpu);
    }
    tribes_teb_clear();tribes_absolute_memory_clear();release_memory(t);
    TribesThreadObject *object=t->object;uint32_t code=t->code;
    tribes_thread_object_finish(object,code);tribes_thread_object_set_current(NULL);self=NULL;
    if(!terminated)check(pthread_detach(pthread_self()));
    check(pthread_mutex_lock(&registry));Thread **link=&threads;
    while(*link!=t)link=&(*link)->next;*link=t->next;--live_threads;
    check(pthread_cond_broadcast(&thread_finished));check(pthread_mutex_unlock(&registry));free(t);
}
static void cancel_thread(void *context){
    Thread *t=context;check(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL));
    if(!atomic_load_explicit(&t->process->exiting,memory_order_acquire)||t->phase!=2)stop("cancellation outside process shutdown");
    t->code=t->process->exit_code;retire_thread(t,1);
}
static void *run(void *context){
    check(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL));
    Thread *t=context;self=t;tribes_thread_object_set_current(t->object);
    /* Suspension prevents original TLS/DLL callbacks as well as the entry. */
    tribes_thread_object_wait_start(t->object);
    uint64_t base=t->process->base;t->cpu._pointer_offset=base;t->cpu._esp=t->stack_top;t->cpu._eflags=0x202;
    if(!tribes_absolute_memory_bind(base))stop("absolute-memory thread binding");
    t->cpu.stack_bottom=(void *)(uintptr_t)(base+t->stack_bottom);t->cpu.stack_top=(void *)(uintptr_t)(base+t->stack_top);
    t->cpu._st_cw=0x27f;t->cpu._st_tag=0xffff;t->cpu._st_extended=1;t->cpu._mxcsr=0x1f80;
    for(ThreadModule *m=t->process->modules;m;m=m->next)if(m->source.tls_index!=UINT32_MAX){
        uint32_t block=t->environment+m->offset;
        if(m->source.template_size)memcpy((void *)(uintptr_t)(base+block),m->source.template_bytes,m->source.template_size);
        put(base,t->environment+4096+4*m->source.tls_index,block);
    }
    if(!tribes_teb_configure(base,t->environment,t->stack_top,t->stack_bottom,t->process->pid,t->id,t->environment+4096))stop("TEB initialization");
    tribes_set_last_error(0);tribes_loader_lock();while(!t->process->ready)check(pthread_cond_wait(&initialized,&loader));
    t->phase=1;for(ThreadModule *m=t->process->modules;m;m=m->next)notify_module(t,m,2);
    check(pthread_mutex_unlock(&loader));t->phase=2;
    pthread_cleanup_push(cancel_thread,t);
    if(!_setjmp(t->escape))t->code=tribes_call_guest(&t->cpu,t->start,1,&t->parameter,t->stack_top);
    pthread_cleanup_pop(0);
    retire_thread(t,0);return NULL;
}
uint32_t tribes_create_thread(uint64_t base,uint32_t attributes,uint32_t size,uint32_t start,uint32_t parameter,uint32_t flags,uint32_t output){
    if(attributes)stop("security attributes not yet implemented");
    if((flags&~0x10004u)||(output&&(output>UINT32_MAX-3||base>UINTPTR_MAX-output-3))){tribes_set_last_error(87);return 0;}
    uint32_t saved=tribes_get_last_error();check(pthread_mutex_lock(&registry));Process *p=find(base);
    if(!p)stop("CreateThread before host process configuration");
    if(p->exiting){check(pthread_mutex_unlock(&registry));tribes_set_last_error(1115);return 0;}
    uint32_t id=next_id;if(id==p->main_tid)++id;
    if(!id||id==UINT32_MAX){check(pthread_mutex_unlock(&registry));tribes_set_last_error(8);return 0;}
    next_id=id+1;check(pthread_mutex_unlock(&registry));
    uint64_t reserve;
    if(flags&0x10000)reserve=size?((uint64_t)size+65535)&~UINT64_C(65535):p->reserve;
    else reserve=size>=p->reserve?((uint64_t)size+0xfffff)&~UINT64_C(0xfffff):p->reserve;
    if(!reserve||reserve+65536>UINT32_MAX){tribes_set_last_error(8);return 0;}
    Thread *t=calloc(1,sizeof *t);if(!t){tribes_set_last_error(8);return 0;}
    t->process=p;t->id=id;t->start=start;t->parameter=parameter;
    t->stack=tribes_virtual_alloc(base,0,(uint32_t)reserve+65536,0x2000,4);
    if(!t->stack)goto failure;
    t->stack_bottom=t->stack+65536;t->stack_top=t->stack_bottom+(uint32_t)reserve;
    if(!tribes_virtual_alloc(base,t->stack_bottom,(uint32_t)reserve,0x1000,4))goto failure;
    t->environment=tribes_virtual_alloc(base,0,p->environment_size,0x3000,4);if(!t->environment)goto failure;
    uint32_t handle;t->object=tribes_thread_object_create(id,!!(flags&4),&handle);if(!t->object)goto failure;
    pthread_attr_t a;int error=pthread_attr_init(&a);
    if(!error){error=pthread_attr_setstacksize(&a,16u*1024*1024);
        if(!error){
            check(pthread_mutex_lock(&registry));
            if(p->exiting)error=ECANCELED;
            else {
                ++live_threads;t->next=threads;threads=t;
                error=pthread_create(&t->native,&a,run,t);
                if(error){threads=t->next;--live_threads;}
            }
            check(pthread_mutex_unlock(&registry));
        }
        check(pthread_attr_destroy(&a));
    }
    if(error){if(!tribes_close_handle(handle))stop("failed create handle release");tribes_thread_object_finish(t->object,0);goto failure;}
    /* The child can complete and free t before this point; use saved values. */
    if(output)put(base,output,id);tribes_set_last_error(saved);return handle;
failure:release_memory(t);free(t);tribes_set_last_error(8);return 0;
}
/* The original non-IFS socket fallback queues WT_EXECUTELONGFUNCTION waits.
   Give each wait an independent worker through the existing AOT thread
   lifecycle. No persistent worker, APC or impersonation contract is claimed.
   The running-owner reference outlives the private handle closed here. */
uint32_t tribes_queue_user_work_item(uint64_t base,uint32_t start,uint32_t parameter,uint32_t flags){
    if(flags!=0x10)stop("unsupported QueueUserWorkItem flags");
    if(!start){tribes_set_last_error(87);return 0;}
    uint32_t saved=tribes_get_last_error();
    uint32_t handle=tribes_create_thread(base,0,0,start,parameter,0,0);
    if(!handle)return 0;
    if(!tribes_close_handle(handle))stop("queued worker handle release");
    tribes_set_last_error(saved);return 1;
}
_Noreturn void tribes_exit_thread(_cpu *cpu,uint32_t code){
    if(!self||self->phase!=2||!cpu||cpu->_pointer_offset!=self->process->base)stop("ExitThread outside running guest procedure");
    self->cpu=*cpu;self->code=code;tribes_guest_callback_reset();_longjmp(self->escape,1);
}
_Noreturn void tribes_free_library_and_exit_thread(_cpu *cpu,uint32_t module,uint32_t code){
    (void)tribes_free_library(cpu->_pointer_offset,module);tribes_exit_thread(cpu,code);
}
uint32_t tribes_guest_threads_live_count(void){check(pthread_mutex_lock(&registry));uint32_t count=live_threads;check(pthread_mutex_unlock(&registry));return count;}

/* The reached main-thread shutdown cancels workers only at native waits with
   explicit cleanup. Other running/blocked states remain bounded failures; no
   asynchronous native cancellation or fabricated thread completion. */
static void process_detach(_cpu *cpu,ThreadModule *m){
    if(!m)return;process_detach(cpu,m->next);if(m->active!=1)return;
    uint32_t args[]={m->source.image,0,1};
    for(uint32_t i=0;i<m->source.callback_count;i++)
        (void)tribes_call_guest(cpu,m->source.callbacks[i],3,args,cpu->_esp-64);
    if(m->source.entry)(void)tribes_call_guest(cpu,m->source.entry,3,args,cpu->_esp-64);
    m->active=0;
}
_Noreturn void tribes_exit_process(_cpu *cpu,uint32_t code){
    if(!cpu||self)stop("ExitProcess requires the main guest thread");
    check(pthread_mutex_lock(&registry));Process *p=find(cpu->_pointer_offset);
    if(!p||!p->ready||p!=processes||p->next||p->exiting||tribes_get_current_thread_id()!=p->main_tid)
        stop("unsupported process shutdown state");
    for(Thread *t=threads;t;t=t->next)if(t->process!=p||t->phase!=2)stop("worker not ready for process shutdown");
    p->exit_code=code;atomic_store_explicit(&p->exiting,1,memory_order_release);
    uint32_t count=live_threads;pthread_t *workers=count?calloc(count,sizeof *workers):NULL;
    if(count&&!workers)stop("shutdown worker allocation");
    uint32_t index=0;
    for(Thread *t=threads;t;t=t->next){workers[index++]=t->native;check(pthread_cancel(t->native));}
    struct timespec deadline;check(clock_gettime(CLOCK_MONOTONIC,&deadline));deadline.tv_sec+=10;
    while(live_threads){
        struct timespec now,left;check(clock_gettime(CLOCK_MONOTONIC,&now));
        left.tv_sec=deadline.tv_sec-now.tv_sec;left.tv_nsec=deadline.tv_nsec-now.tv_nsec;
        if(left.tv_nsec<0){--left.tv_sec;left.tv_nsec+=1000000000;}
        if(left.tv_sec<0||(!left.tv_sec&&!left.tv_nsec))stop("workers did not reach cancellable waits");
        int error=pthread_cond_timedwait_relative_np(&thread_finished,&registry,&left);
        if(error&&error!=ETIMEDOUT)check(error);
    }
    check(pthread_mutex_unlock(&registry));
    /* Joining also completes native TLS destructors before original DLL detach. */
    for(uint32_t i=0;i<count;i++){void *result;check(pthread_join(workers[i],&result));if(result!=PTHREAD_CANCELED)stop("worker escaped shutdown cancellation");}
    free(workers);
    tribes_loader_lock();process_detach(cpu,p->modules);tribes_loader_unlock();
    fprintf(stderr,"TribesPad: original process shutdown completed, code=%u\n",code);
    _Exit((int)(code&255));
}
