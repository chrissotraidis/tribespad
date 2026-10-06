#include "guest_console.h"
#include "guest_heap.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct Handler {uint32_t address;struct Handler *next;} Handler;
typedef struct Process {uint64_t base;int ignore_c;Handler *handlers;struct Process *next;} Process;
struct TribesConsoleDispatch {size_t count,index;int ignored;uint32_t handlers[];};
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Process *processes;
static Process *find(uint64_t base){for(Process *p=processes;p;p=p->next)if(p->base==base)return p;return NULL;}
uint32_t tribes_set_console_ctrl_handler(uint64_t base,uint32_t handler,uint32_t add){
    pthread_mutex_lock(&lock);Process *p=find(base);
    if(!p){
        if(handler&&!add){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
        p=calloc(1,sizeof *p);
        if(!p){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
        p->base=base;p->next=processes;processes=p;
    }
    if(!handler)p->ignore_c=!!add;
    else if(add){
        Handler *h=malloc(sizeof *h);
        if(!h){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
        h->address=handler;h->next=p->handlers;p->handlers=h;
    }else{
        Handler **link=&p->handlers;while(*link&&(*link)->address!=handler)link=&(*link)->next;
        if(!*link){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
        Handler *h=*link;*link=h->next;free(h);
    }
    pthread_mutex_unlock(&lock);return 1;
}
int tribes_console_begin(uint64_t base,uint32_t event,TribesConsoleDispatch **output){
    if(!output||(event!=0&&event!=1&&event!=2&&event!=5&&event!=6)){tribes_set_last_error(87);return 0;}
    pthread_mutex_lock(&lock);Process *p=find(base);size_t count=0;int ignored=p&&p->ignore_c&&event==0;
    if(p&&!ignored)for(Handler *h=p->handlers;h;h=h->next)count++;
    if(count>(SIZE_MAX-sizeof(TribesConsoleDispatch))/sizeof(uint32_t)){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
    TribesConsoleDispatch *d=malloc(sizeof *d+count*sizeof(uint32_t));
    if(!d){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
    d->count=count;d->index=0;d->ignored=ignored;size_t i=0;
    if(p&&!ignored)for(Handler *h=p->handlers;h;h=h->next)d->handlers[i++]=h->address;
    pthread_mutex_unlock(&lock);*output=d;return 1;
}
uint32_t tribes_console_next(TribesConsoleDispatch *d){return d&&d->index<d->count?d->handlers[d->index++]:0;}
int tribes_console_ignored(const TribesConsoleDispatch *d){return d&&d->ignored;}
void tribes_console_end(TribesConsoleDispatch *d){free(d);}
void tribes_console_destroy(uint64_t base){
    pthread_mutex_lock(&lock);Process **link=&processes;while(*link&&(*link)->base!=base)link=&(*link)->next;
    if(*link){Process *p=*link;*link=p->next;while(p->handlers){Handler *h=p->handlers;p->handlers=h->next;free(h);}free(p);}
    pthread_mutex_unlock(&lock);
}
