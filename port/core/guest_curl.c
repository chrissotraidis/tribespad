#include "guest_curl.h"
#include "guest_curl_allocator.h"
#include "native_winsock.h"
#include "guest_heap.h"
#include "guest_filesystem.h"
#include "guest_memory_checks.h"
#include <curl/curl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Poll { int fd;uint32_t guest,private;struct Poll *next; } Poll;
typedef struct Socket { int fd;uint32_t guest;struct Socket *next; } Socket;
typedef struct Message {uint32_t guest;struct Message *next;} Message;
typedef struct Multi {
    uint32_t guest,timer_function,timer_data,socket_function,socket_data;
    CURLM *native;
    Poll *polls;
    Message *messages;
    struct Multi *next;
} Multi;
typedef struct ListNode {struct curl_slist *native;uint32_t *public,guest;struct ListNode *next;} ListNode;
typedef struct StringList {ListNode *head,*tail;struct StringList *next;} StringList;
typedef struct Url {uint32_t guest;CURLU *native;struct Url *next;} Url;
typedef struct Easy { uint32_t guest,private,write_function,write_data,header_function,header_data,progress_function,progress_data,error_address,ca_info,headers,url; char error_buffer[CURL_ERROR_SIZE]; int stale_headers,stale_url; int64_t post_size; CURL *native; Multi *borrowed,*attached; struct Easy *next; } Easy;
typedef struct {
    uint64_t base;
    pthread_t owner;
    TribesCurlAllocator *allocator;
    unsigned references;
    Multi *multis;
    Easy *easies;
    StringList *lists;
    Url *urls;
    Socket *sockets;
    TribesCurlFrame *active;
    unsigned callbacks;
} State;
static State *state;
/* All registry/native operations are serialized, including callbacks and
   early returns. This initially also serializes different handles; latency
   during blocking operations must be measured before gameplay acceptance. */
static pthread_mutex_t lock=PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
static unsigned calls;
typedef struct Version { uint64_t base;uint32_t guest;struct Version *next; } Version;
static Version *versions;
static pthread_mutex_t version_lock=PTHREAD_MUTEX_INITIALIZER;
static _Noreturn void stop(void) { fputs("TribesPad: unsupported curl runtime ownership/lifecycle\n",stderr); abort(); }
uint32_t tribes_curl_version(_cpu *cpu) {
    if(!cpu)stop();
    /* Pinned curl version.c returns a 300-byte static buffer and rebuilds its
       text on every call. The selected LibreSSL/zlib version path performs no
       curl allocation and is available before global initialization. Serialize
       that host buffer and maintain separate library-lifetime guest storage;
       neither the native pointer nor a curl-owned allocation is returned. */
    pthread_mutex_lock(&version_lock);
    const char *native=curl_version();if(!native)stop();
    size_t length=strnlen(native,300);if(length==300)stop();
    Version *v=versions;while(v&&v->base!=cpu->_pointer_offset)v=v->next;
    if(!v) {
        v=calloc(1,sizeof *v);if(!v)stop();
        v->base=cpu->_pointer_offset;v->guest=tribes_global_alloc(v->base,0,300);
        if(!v->guest)stop();
        v->next=versions;versions=v;
    }
    memcpy((void *)(uintptr_t)(v->base+v->guest),native,length+1);
    uint32_t result=v->guest;pthread_mutex_unlock(&version_lock);return result;
}
/* Global callbacks belong to one guest runtime. The scoped lease spans handle
   lookup, native entry, callbacks and result publication. Recursive callbacks
   retain that lease; other guest threads wait before accessing native state. */
static State *get(_cpu *cpu,int initialized) {
    pthread_mutex_lock(&lock);
    State *s=state;
    if(!cpu || !s || s->base!=cpu->_pointer_offset ||
       (initialized && !s->references)) stop();
    if(calls==UINT_MAX)stop();
    if(!calls)s->owner=pthread_self();
    else if(!pthread_equal(s->owner,pthread_self()))stop();
    calls++;return s;
}
static void release(State **unused) {
    (void)unused; /* Final global cleanup may already have retired State. */
    if(!calls)stop();calls--;if(pthread_mutex_unlock(&lock))stop();
}
#define CURL_SCOPE State *s __attribute__((cleanup(release)))
static void sync_errors(State *s,int to_guest);
static void enter(State *s,_cpu *cpu,TribesCurlFrame *frame) {
    sync_errors(s,0);
    tribes_curl_allocator_enter(s->allocator,cpu,frame); s->active=frame;
}
static void leave(State *s,TribesCurlFrame *frame) {
    if(s->active!=frame) stop();
    sync_errors(s,1);
    s->active=frame->previous; tribes_curl_allocator_leave(frame);
}
static Multi *multi(State *s,uint32_t handle) {
    for(Multi *m=s->multis;m;m=m->next) if(m->guest==handle) return m;
    return NULL;
}
static Easy *easy(State *s,uint32_t handle) {
    for(Easy *e=s->easies;e;e=e->next) if(e->guest==handle) return e;
    return NULL;
}
static void *guest(State *s,uint32_t address,uint32_t size) {
    if(!address||address>UINT32_MAX-(size-1)||s->base>UINTPTR_MAX-address-(size-1))stop();
    return (void *)(uintptr_t)(s->base+address);
}
static void retire_messages(State *s,Multi *m){
    while(m->messages){Message *entry=m->messages;m->messages=entry->next;
        if(tribes_global_free(s->base,entry->guest))stop();free(entry);
    }
}
/* Curl retains error-buffer pointers. Native storage never aliases guest
   memory; synchronize at API and callback boundaries. Registered buffers must
   remain readable/writable until unset or easy cleanup. Overlapping buffers
   across handles need shared storage and are not currently admitted. */
static void sync_errors(State *s,int to_guest){
    for(Easy *e=s->easies;e;e=e->next)if(e->error_address){
        void *p=guest(s,e->error_address,CURL_ERROR_SIZE);
        uint32_t token=tribes_memory_access_begin(p,CURL_ERROR_SIZE,3);
        if(to_guest)memcpy(p,e->error_buffer,CURL_ERROR_SIZE);
        else memcpy(e->error_buffer,p,CURL_ERROR_SIZE);
        tribes_memory_access_end(token);
    }
}
/* The native list and its pointer-bearing nodes stay private. A separate
   two-word public node exposes the original i686 data/next layout. Curl's
   registered allocator owns both, and validates its immutable guest copies. */
static StringList **list_slot(State *s,uint32_t handle){
    StringList **p=&s->lists;while(*p&&(*p)->head->guest!=handle)p=&(*p)->next;
    if(!*p)stop();return p;
}
static void list_idle(State *s,uint32_t handle){
    if(calls!=1||s->callbacks)stop();
    for(Easy *e=s->easies;e;e=e->next)if(handle&&e->headers==handle&&e->attached)stop();
}
uint32_t tribes_curl_slist_append(_cpu *cpu,uint32_t handle,uint32_t text){
    CURL_SCOPE=get(cpu,1);StringList *list=handle?*list_slot(s,handle):NULL;list_idle(s,handle);
    if(!text)stop();uint32_t length=0;
    while(length<1048576&&(uint64_t)text+length<UINT64_C(0x100000000)){
        if(!*(const char *)guest(s,text+length,1))break;length++;
    }
    if(length==1048576||(uint64_t)text+length==UINT64_C(0x100000000))stop();
    char *copy=malloc((size_t)length+1);ListNode *node=calloc(1,sizeof *node);
    if(!list)list=calloc(1,sizeof *list);
    if(!copy||!node||!list){free(copy);free(node);if(!handle)free(list);return 0;}
    memcpy(copy,guest(s,text,length+1),length+1);
    TribesCurlFrame frame;enter(s,cpu,&frame);
    node->public=tribes_curl_host_calloc(2,4);
    if(node->public)node->native=curl_slist_append(NULL,copy);
    free(copy);
    if(!node->native){if(node->public)tribes_curl_host_free(node->public);leave(s,&frame);free(node);if(!handle)free(list);return 0;}
    node->guest=tribes_curl_allocator_address(node->public);
    node->public[0]=tribes_curl_allocator_address(node->native->data);
    if(list->tail){list->tail->native->next=node->native;list->tail->public[1]=node->guest;list->tail->next=node;}
    else {list->head=node;list->next=s->lists;s->lists=list;}
    list->tail=node;uint32_t result=list->head->guest;leave(s,&frame);return result;
}
void tribes_curl_slist_free_all(_cpu *cpu,uint32_t handle){
    CURL_SCOPE=get(cpu,1);if(!handle)return;StringList **slot=list_slot(s,handle);StringList *list=*slot;list_idle(s,handle);
    TribesCurlFrame frame;enter(s,cpu,&frame);
    /* Free-after-perform/before-cleanup is valid. Clear native references, but
       reject another transfer until the caller sets a new list (or NULL). */
    for(Easy *e=s->easies;e;e=e->next)if(e->headers==handle){
        if(curl_easy_setopt(e->native,CURLOPT_HTTPHEADER,NULL)!=CURLE_OK)stop();
        e->headers=0;e->stale_headers=1;
    }
    curl_slist_free_all(list->head->native);
    while(list->head){ListNode *n=list->head;list->head=n->next;tribes_curl_host_free(n->public);free(n);}
    *slot=list->next;free(list);leave(s,&frame);
}
/* Opaque native URL objects use the registered allocator's guest identity.
   Input strings are copied before libcurl runs; it never sees guest pointers. */
static Url **url_slot(State *s,uint32_t handle){
    Url **p=&s->urls;while(*p&&(*p)->guest!=handle)p=&(*p)->next;
    if(!*p)stop();return p;
}
static void url_idle(State *s,uint32_t handle){
    if(calls!=1||s->callbacks)stop();
    for(Easy *e=s->easies;e;e=e->next)if(e->url==handle&&e->attached)stop();
}
uint32_t tribes_curl_url(_cpu *cpu){
    CURL_SCOPE=get(cpu,1);Url *u=calloc(1,sizeof *u);if(!u)return 0;
    TribesCurlFrame frame;enter(s,cpu,&frame);u->native=curl_url();
    if(u->native){u->guest=tribes_curl_allocator_address(u->native);u->next=s->urls;s->urls=u;}
    leave(s,&frame);if(!u->native){free(u);return 0;}return u->guest;
}
uint32_t tribes_curl_url_set(_cpu *cpu,uint32_t handle,uint32_t part,uint32_t text,uint32_t flags){
    CURL_SCOPE=get(cpu,1);if(!handle)return CURLUE_BAD_HANDLE;
    Url *u=*url_slot(s,handle);url_idle(s,handle);char *copy=NULL;
    if(text){
        const char *source=guest(s,text,1);size_t limit=UINT64_C(0x100000000)-text;
        size_t n=strnlen(source,limit);if(n==limit)stop();
        copy=malloc(n+1);if(!copy)return CURLUE_OUT_OF_MEMORY;memcpy(copy,source,n+1);
    }
    TribesCurlFrame frame;enter(s,cpu,&frame);
    CURLUcode result=curl_url_set(u->native,(CURLUPart)part,copy,flags);
    leave(s,&frame);free(copy);return (uint32_t)result;
}
void tribes_curl_url_cleanup(_cpu *cpu,uint32_t handle){
    CURL_SCOPE=get(cpu,1);if(!handle)return;Url **slot=url_slot(s,handle);Url *u=*slot;url_idle(s,handle);
    TribesCurlFrame frame;enter(s,cpu,&frame);
    /* Completed easy handles can outlive their caller-owned URL. Clear native
       references and reject reuse until a replacement is explicitly set. */
    for(Easy *e=s->easies;e;e=e->next)if(e->url==handle){
        if(curl_easy_setopt(e->native,CURLOPT_CURLU,NULL)!=CURLE_OK)stop();
        e->url=0;e->stale_url=1;
    }
    curl_url_cleanup(u->native);*slot=u->next;free(u);leave(s,&frame);
}
static uint32_t dispatch(State *s,uint32_t target,unsigned count,const uint32_t *args) {
    TribesCurlFrame *frame=s->active;if(!frame)stop();
    sync_errors(s,1);
    s->callbacks++;tribes_curl_allocator_guest_begin(frame);
    uint32_t result=tribes_call_guest_cdecl(frame->cpu,target,count,args,frame->cpu->_esp-128);
    tribes_curl_allocator_guest_end(frame);s->callbacks--;sync_errors(s,0);return result;
}
static int close_socket(void *userdata,curl_socket_t fd) {
    State *s=state;if(!s||s!=userdata||!pthread_equal(s->owner,pthread_self())||!s->active)stop();
    Socket **slot=&s->sockets;while(*slot&&(*slot)->fd!=fd)slot=&(*slot)->next;
    if(*slot) {
        Socket *entry=*slot;
        if(tribes_socket_detach(s->base,entry->guest,s)!=fd)stop();
        *slot=entry->next;free(entry);
    }
    /* Pinned curl calls Curl_multi_will_close before this callback. That
       removes its socket entry even when an earlier callback failed before
       the first notification was marked announced (and no REMOVE followed). */
    for(Multi *m=s->multis;m;m=m->next) {
        Poll **p=&m->polls;
        while(*p){Poll *entry=*p;if(entry->fd==fd){*p=entry->next;free(entry);}else p=&entry->next;}
    }
    /* libcurl copies this callback and State userdata into each connection.
       State outlives every easy and multi, including cached connections. */
    return close(fd)?1:0;
}
static int socket_callback(CURL *native,curl_socket_t fd,int what,void *userdata,void *socketp) {
    State *s=state;if(!s||!pthread_equal(s->owner,pthread_self())||!s->active)stop();
    Multi *m=s->multis;while(m&&m!=userdata)m=m->next;
    if(!m||!m->socket_function||fd<0||what<0||what>4)stop();
    Easy *e=s->easies;while(e&&e->native!=native)e=e->next;
    if(!e) {
        /* Curl can supply its internal easy (e.g. shutdown). Retain its
           registry entry only for this multi and verify allocation identity. */
        e=calloc(1,sizeof *e);if(!e)return -1;
        e->native=native;e->guest=tribes_curl_allocator_address(native);e->borrowed=m;
        e->next=s->easies;s->easies=e;
    }
    if(tribes_curl_allocator_address(native)!=e->guest)stop();
    Poll *p=m->polls;while(p&&p->fd!=fd)p=p->next;
    if(p) { if(socketp!=p)stop(); }
    else {
        if(socketp||what==CURL_POLL_REMOVE)stop();
        Socket *sock=s->sockets;while(sock&&sock->fd!=fd)sock=sock->next;
        if(!sock) {
            sock=calloc(1,sizeof *sock);if(!sock)return -1;
            sock->guest=tribes_socket_attach(s->base,fd,s);
            if(sock->guest==UINT32_MAX){free(sock);return -1;}
            sock->fd=fd;sock->next=s->sockets;s->sockets=sock;
        }
        p=calloc(1,sizeof *p);if(!p)return -1;
        p->fd=fd;p->guest=sock->guest;
        CURLMcode assigned=curl_multi_assign(m->native,fd,p);
        if(assigned!=CURLM_OK){free(p);return -1;}
        p->next=m->polls;m->polls=p;
    }
    uint32_t args[]={e->guest,p->guest,(uint32_t)what,m->socket_data,p->private};
    int result=(int32_t)dispatch(s,m->socket_function,5,args);
    if(what==CURL_POLL_REMOVE) {
        /* Removal ends polling and the assigned pointer, not the connection.
           Keep the stable socket handle until close_socket actually closes. */
        if(curl_multi_assign(m->native,fd,NULL)!=CURLM_OK)stop();
        Poll **slot=&m->polls;while(*slot&&*slot!=p)slot=&(*slot)->next;
        if(!*slot)stop();*slot=p->next;free(p);
    }
    return result;
}
static int timer_callback(CURLM *native,long timeout,void *userdata) {
    State *s=state;
    if(!s || !pthread_equal(s->owner,pthread_self()) || !s->active) stop();
    Multi *m=s->multis; while(m && m!=userdata) m=m->next;
    if(!m || m->native!=native || !m->timer_function || timeout<INT32_MIN || timeout>INT32_MAX) stop();
    uint32_t args[]={m->guest,(uint32_t)(int32_t)timeout,m->timer_data};
    return (int32_t)dispatch(s,m->timer_function,3,args);
}
static size_t data_callback(char *data,size_t size,size_t count,void *userdata,int header) {
    State *s=state;if(!s||!s->active||!pthread_equal(s->owner,pthread_self()))stop();
    Easy *e=s->easies;while(e&&e!=userdata)e=e->next;
    if(!e)stop();uint32_t function=header&&e->header_function?e->header_function:e->write_function;
    if(!function)stop(); /* Guest FILE* fallback is not a native FILE*. */
    if(size>UINT32_MAX||count>UINT32_MAX||(size&&count>UINT32_MAX/size))return CURL_WRITEFUNC_ERROR;
    size_t bytes=size*count;
    if(!bytes)return 0;
    uint32_t buffer=tribes_global_alloc(s->base,0,(uint32_t)bytes);if(!buffer)return CURL_WRITEFUNC_ERROR;
    memcpy(guest(s,buffer,(uint32_t)bytes),data,bytes);
    uint32_t args[]={buffer,(uint32_t)size,(uint32_t)count,header?e->header_data:e->write_data};
    uint32_t result=dispatch(s,function,4,args);
    if(tribes_global_free(s->base,buffer))stop();
    return result;
}
static size_t write_callback(char *data,size_t size,size_t count,void *userdata){return data_callback(data,size,count,userdata,0);}
static size_t header_callback(char *data,size_t size,size_t count,void *userdata){return data_callback(data,size,count,userdata,1);}
static int progress_callback(void *userdata,curl_off_t total,curl_off_t now,curl_off_t upload_total,curl_off_t upload_now){
    State *s=state;if(!s||!s->active||!pthread_equal(s->owner,pthread_self()))stop();
    Easy *e=s->easies;while(e&&e!=userdata)e=e->next;if(!e||!e->progress_function)stop();
    uint64_t values[]={(uint64_t)total,(uint64_t)now,(uint64_t)upload_total,(uint64_t)upload_now};
    uint32_t args[9]={e->progress_data};
    for(unsigned i=0;i<4;i++){args[1+2*i]=(uint32_t)values[i];args[2+2*i]=(uint32_t)(values[i]>>32);}
    return (int32_t)dispatch(s,e->progress_function,9,args);
}
uint32_t tribes_curl_global_init_mem(_cpu *cpu,uint32_t flags,uint32_t m,uint32_t f,uint32_t r,uint32_t d,uint32_t c) {
    if(!m || !f || !r || !d || !c) return CURLE_FAILED_INIT;
    if(!cpu) stop();
    pthread_mutex_lock(&lock);
    if(!state) {
        uint32_t callbacks[]={m,f,r,d,c};
        State *s=calloc(1,sizeof *s);
        if(s) s->allocator=tribes_curl_allocator_create_serialized(cpu->_pointer_offset,callbacks);
        if(!s || !s->allocator) { free(s); pthread_mutex_unlock(&lock); return CURLE_FAILED_INIT; }
        s->base=cpu->_pointer_offset; s->owner=pthread_self(); state=s;
    }
    CURL_SCOPE=get(cpu,0);
    pthread_mutex_unlock(&lock); /* retain the scoped recursive acquisition */
    if(calls!=1)stop();
    /* Like native curl, repeated initialization retains the first callbacks. */
    TribesCurlFrame frame; enter(s,cpu,&frame);
    CURLcode result=curl_global_init_mem((long)(int32_t)flags,tribes_curl_host_malloc,tribes_curl_host_free,
                                        tribes_curl_host_realloc,tribes_curl_host_strdup,tribes_curl_host_calloc);
    leave(s,&frame);
    if(result==CURLE_OK) s->references++;
    else if(!s->references) {
        /* A retry may supply a different table. Retire a failed initialization
           only after proving it did not leave guest-owned allocations behind. */
        tribes_curl_allocator_destroy(s->allocator);
        pthread_mutex_lock(&lock); state=NULL; pthread_mutex_unlock(&lock); free(s);
    }
    return (uint32_t)result;
}
uint32_t tribes_curl_multi_init(_cpu *cpu) {
    CURL_SCOPE=get(cpu,1); Multi *entry=calloc(1,sizeof *entry); if(!entry) return 0;
    TribesCurlFrame frame; enter(s,cpu,&frame);
    CURLM *native=curl_multi_init();
    if(native) {
        entry->native=native; entry->guest=tribes_curl_allocator_address(native);
        entry->next=s->multis; s->multis=entry;
        /* Native userdata always names our registry entry. A guest pointer is
           held as a 32-bit value and only delivered back to guest code. */
        if(curl_multi_setopt(native,CURLMOPT_TIMERDATA,entry)!=CURLM_OK) stop();
        if(curl_multi_setopt(native,CURLMOPT_SOCKETDATA,entry)!=CURLM_OK) stop();
    }
    leave(s,&frame);
    if(!native) { free(entry); return 0; }
    return entry->guest;
}
uint32_t tribes_curl_multi_cleanup(_cpu *cpu,uint32_t handle) {
    CURL_SCOPE=get(cpu,1); Multi **slot=&s->multis;
    while(*slot && (*slot)->guest!=handle) slot=&(*slot)->next;
    if(!*slot) return CURLM_BAD_HANDLE;
    Multi *entry=*slot;
    TribesCurlFrame frame; enter(s,cpu,&frame);
    CURLMcode result=curl_multi_cleanup(entry->native);
    leave(s,&frame);
    if(result==CURLM_OK) {
        retire_messages(s,entry);
        for(Easy *e=s->easies;e;e=e->next)if(e->attached==entry)e->attached=NULL;
        /* Guest callbacks may create other handles during native cleanup. */
        slot=&s->multis; while(*slot && *slot!=entry) slot=&(*slot)->next;
        if(!*slot) stop();
        while(entry->polls){Poll *p=entry->polls;entry->polls=p->next;free(p);}
        Easy **es=&s->easies;while(*es){Easy *e=*es;if(e->borrowed==entry){*es=e->next;free(e);}else es=&e->next;}
        *slot=entry->next; free(entry);
    }
    return (uint32_t)result;
}
void tribes_curl_global_cleanup(_cpu *cpu) {
    CURL_SCOPE=get(cpu,1);
    if(calls!=1 || s->callbacks || (s->references==1 && (s->multis || s->easies || s->sockets || s->lists || s->urls))) stop();
    TribesCurlFrame frame; enter(s,cpu,&frame);
    curl_global_cleanup(); s->references--;
    leave(s,&frame);
    if(!s->references) {
        tribes_curl_allocator_destroy(s->allocator);
        pthread_mutex_lock(&lock); state=NULL; pthread_mutex_unlock(&lock); free(s);
    }
}
uint32_t tribes_curl_easy_init(_cpu *cpu) {
    CURL_SCOPE=get(cpu,1); Easy *entry=calloc(1,sizeof *entry); if(!entry) return 0; entry->post_size=-1;
    TribesCurlFrame frame; enter(s,cpu,&frame);
    CURL *native=curl_easy_init();
    if(native) {
        entry->native=native; entry->guest=tribes_curl_allocator_address(native);
        entry->next=s->easies; s->easies=entry;
        if(curl_easy_setopt(native,CURLOPT_NOSIGNAL,1L)!=CURLE_OK)stop();
        if(curl_easy_setopt(native,CURLOPT_CLOSESOCKETFUNCTION,close_socket)!=CURLE_OK||
           curl_easy_setopt(native,CURLOPT_CLOSESOCKETDATA,s)!=CURLE_OK)stop();
    }
    leave(s,&frame);
    if(!native) { free(entry); return 0; }
    return entry->guest;
}
void tribes_curl_easy_reset(_cpu *cpu,uint32_t handle) {
    CURL_SCOPE=get(cpu,1);
    Easy *entry=easy(s,handle);
    /* The reached pool resets detached owned handles. Recursive, borrowed,
       and attached resets require separate lifecycle evidence. */
    if(!entry||s->callbacks||entry->borrowed||entry->attached)stop();
    TribesCurlFrame frame; enter(s,cpu,&frame);
    curl_easy_reset(entry->native);
    if(entry->ca_info&&tribes_global_free(s->base,entry->ca_info))stop();
    *entry=(Easy){.guest=entry->guest,.native=entry->native,.next=entry->next,.post_size=-1};
    /* Reset guest options, but retain the bridge's socket ownership and
       signal policy. Native reset preserves the real connection/DNS caches. */
    if(curl_easy_setopt(entry->native,CURLOPT_NOSIGNAL,1L)!=CURLE_OK||
       curl_easy_setopt(entry->native,CURLOPT_CLOSESOCKETFUNCTION,close_socket)!=CURLE_OK||
       curl_easy_setopt(entry->native,CURLOPT_CLOSESOCKETDATA,s)!=CURLE_OK)stop();
    leave(s,&frame);
}
void tribes_curl_easy_cleanup(_cpu *cpu,uint32_t handle) {
    CURL_SCOPE=get(cpu,1); if(!handle) return;
    /* A void native cleanup can silently reject recursive use. Never retire
       a still-live registry entry when that native call did not free it. */
    if(s->callbacks) stop();
    Easy **slot=&s->easies; while(*slot && (*slot)->guest!=handle) slot=&(*slot)->next;
    if(!*slot) stop();
    Easy *entry=*slot;
    if(entry->borrowed)stop();
    TribesCurlFrame frame; enter(s,cpu,&frame);
    if(entry->attached)stop();
    curl_easy_cleanup(entry->native);
    leave(s,&frame);
    slot=&s->easies; while(*slot && *slot!=entry) slot=&(*slot)->next;
    if(!*slot) stop();
    *slot=entry->next;if(entry->ca_info&&tribes_global_free(s->base,entry->ca_info))stop();free(entry);
}
uint32_t tribes_curl_multi_add_handle(_cpu *cpu,uint32_t mh,uint32_t eh) {
    CURL_SCOPE=get(cpu,1); Multi *m=multi(s,mh); if(!m) return CURLM_BAD_HANDLE;
    Easy *e=easy(s,eh); if(!e||e->borrowed) return CURLM_BAD_EASY_HANDLE;
    TribesCurlFrame frame; enter(s,cpu,&frame);
    if(e->stale_headers||e->stale_url||(!e->write_function&&e->write_data)||(!e->header_function&&e->header_data&&!e->write_function))stop();
    CURLMcode result=curl_multi_add_handle(m->native,e->native);
    if(result==CURLM_OK)e->attached=m;
    leave(s,&frame); return (uint32_t)result;
}
uint32_t tribes_curl_multi_remove_handle(_cpu *cpu,uint32_t mh,uint32_t eh) {
    CURL_SCOPE=get(cpu,1); Multi *m=multi(s,mh); if(!m) return CURLM_BAD_HANDLE;
    Easy *e=easy(s,eh); if(!e||e->borrowed) return CURLM_BAD_EASY_HANDLE;
    TribesCurlFrame frame; enter(s,cpu,&frame);
    CURLMcode result=curl_multi_remove_handle(m->native,e->native);
    if(result==CURLM_OK){e->attached=NULL;retire_messages(s,m);}
    leave(s,&frame); return (uint32_t)result;
}
uint32_t tribes_curl_multi_setopt(_cpu *cpu,uint32_t handle,uint32_t option,uint32_t value) {
    CURL_SCOPE=get(cpu,1); Multi *m=multi(s,handle); if(!m) return CURLM_BAD_HANDLE;
    TribesCurlFrame frame; enter(s,cpu,&frame); CURLMcode result;
    switch(option) {
        case CURLMOPT_SOCKETFUNCTION:
            if(!value&&m->polls)stop(); /* Disabling with live poll userdata needs cancellation. */
            result=curl_multi_setopt(m->native,CURLMOPT_SOCKETFUNCTION,value?socket_callback:NULL);
            if(result==CURLM_OK)m->socket_function=value;
            break;
        case CURLMOPT_SOCKETDATA:
            result=curl_multi_setopt(m->native,CURLMOPT_SOCKETDATA,m);
            if(result==CURLM_OK)m->socket_data=value;
            break;
        case CURLMOPT_TIMERFUNCTION:
            result=curl_multi_setopt(m->native,CURLMOPT_TIMERFUNCTION,value?timer_callback:NULL);
            if(result==CURLM_OK) m->timer_function=value;
            break;
        case CURLMOPT_TIMERDATA:
            result=curl_multi_setopt(m->native,CURLMOPT_TIMERDATA,m);
            if(result==CURLM_OK) m->timer_data=value;
            break;
        default:
            fprintf(stderr,"TribesPad: unsupported curl multi option %u\n",option); abort();
    }
    leave(s,&frame); return (uint32_t)result;
}
uint32_t tribes_curl_multi_assign(_cpu *cpu,uint32_t handle,uint32_t socket,uint32_t value) {
    CURL_SCOPE=get(cpu,1);Multi *m=multi(s,handle);if(!m)return CURLM_BAD_HANDLE;
    Poll *p=m->polls;while(p&&p->guest!=socket)p=p->next;if(!p)return CURLM_BAD_SOCKET;
    TribesCurlFrame frame;enter(s,cpu,&frame);
    CURLMcode result=curl_multi_assign(m->native,p->fd,p);
    if(result==CURLM_OK)p->private=value;
    leave(s,&frame);return (uint32_t)result;
}
uint32_t tribes_curl_multi_socket_action(_cpu *cpu,uint32_t handle,uint32_t socket,uint32_t events,uint32_t output) {
    CURL_SCOPE=get(cpu,1);Multi *m=multi(s,handle);if(!m)return CURLM_BAD_HANDLE;
    int fd=CURL_SOCKET_TIMEOUT;
    if(socket!=UINT32_MAX){Socket *p=s->sockets;while(p&&p->guest!=socket)p=p->next;if(!p)return CURLM_BAD_SOCKET;fd=p->fd;}
    int running;void *destination=guest(s,output,4);memcpy(&running,destination,4);
    TribesCurlFrame frame;enter(s,cpu,&frame);
    CURLMcode result=curl_multi_socket_action(m->native,fd,(int)events,&running);
    leave(s,&frame);memcpy(destination,&running,4);return (uint32_t)result;
}
/* A native CURLMsg contains 64-bit host pointers. Keep it private and expose
   the original i686 three-word record until remove/cleanup invalidates it.
   In callbacks, native curl's recursion guard returns NULL and count zero. */
uint32_t tribes_curl_multi_info_read(_cpu *cpu,uint32_t handle,uint32_t output){
    CURL_SCOPE=get(cpu,1);void *destination=guest(s,output,4);
    uint32_t token=tribes_memory_access_begin(destination,4,2);
    Multi *m=multi(s,handle);int remaining=0;uint32_t result=0;
    if(m){
        TribesCurlFrame frame;enter(s,cpu,&frame);
        CURLMsg *native=curl_multi_info_read(m->native,&remaining);
        if(native){
            if(native->msg!=CURLMSG_DONE||remaining<0)stop();
            Easy *e=s->easies;while(e&&e->native!=native->easy_handle)e=e->next;
            if(!e||e->borrowed||e->attached!=m||tribes_curl_allocator_address(e->native)!=e->guest)stop();
            Message *entry=calloc(1,sizeof *entry);if(!entry)stop();
            entry->guest=tribes_global_alloc(s->base,0,12);if(!entry->guest)stop();
            uint32_t words[]={(uint32_t)native->msg,e->guest,(uint32_t)native->data.result};
            memcpy(guest(s,entry->guest,12),words,12);
            entry->next=m->messages;m->messages=entry;result=entry->guest;
        }
        leave(s,&frame);
    }
    memcpy(destination,&remaining,4);tribes_memory_access_end(token);return result;
}
uint32_t tribes_curl_easy_setopt(_cpu *cpu,uint32_t handle,uint32_t option,uint32_t value) {
    CURL_SCOPE=get(cpu,1);Easy *e=easy(s,handle);if(!e||e->borrowed)return CURLE_BAD_FUNCTION_ARGUMENT;
    char *text=NULL;
    if(value&&(option==CURLOPT_URL||option==CURLOPT_NOPROXY||option==CURLOPT_CAINFO||option==CURLOPT_USERAGENT)) {
        const char *source=guest(s,value,1);size_t limit=UINT64_C(0x100000000)-value;
        size_t length=strnlen(source,limit);if(length==limit)stop();
        text=malloc(length+1);if(!text)return CURLE_OUT_OF_MEMORY;memcpy(text,source,length+1);
    }
    char ca_path[PATH_MAX];
    if(option==CURLOPT_CAINFO&&text){
        if(!tribes_filesystem_resolve(s->base,text,ca_path,sizeof ca_path)){free(text);return CURLE_SSL_CACERT_BADFILE;}
    }
    TribesCurlFrame frame;enter(s,cpu,&frame);CURLcode result;
    switch(option) {
        case CURLOPT_ERRORBUFFER: {
            if(e->attached||s->callbacks)stop();
            if(value){
                void *p=guest(s,value,CURL_ERROR_SIZE);
                for(Easy *other=s->easies;other;other=other->next)if(other!=e&&other->error_address&&
                    (uint64_t)value<other->error_address+(uint64_t)CURL_ERROR_SIZE&&
                    (uint64_t)other->error_address<value+(uint64_t)CURL_ERROR_SIZE)stop();
                uint32_t token=tribes_memory_access_begin(p,CURL_ERROR_SIZE,3);
                memcpy(e->error_buffer,p,CURL_ERROR_SIZE);tribes_memory_access_end(token);
            }
            result=curl_easy_setopt(e->native,CURLOPT_ERRORBUFFER,value?e->error_buffer:NULL);
            if(result==CURLE_OK)e->error_address=value;break;
        }
        case CURLOPT_HEADERFUNCTION: {
            if(s->callbacks)stop();int enabled=value||e->header_data;
            result=curl_easy_setopt(e->native,CURLOPT_HEADERDATA,enabled?e:NULL);
            if(result==CURLE_OK)result=curl_easy_setopt(e->native,CURLOPT_HEADERFUNCTION,enabled?header_callback:NULL);
            if(result==CURLE_OK)e->header_function=value;break;
        }
        case CURLOPT_HEADERDATA: {
            if(s->callbacks)stop();int enabled=value||e->header_function;
            result=curl_easy_setopt(e->native,CURLOPT_HEADERDATA,enabled?e:NULL);
            if(result==CURLE_OK)result=curl_easy_setopt(e->native,CURLOPT_HEADERFUNCTION,enabled?header_callback:NULL);
            if(result==CURLE_OK)e->header_data=value;break;
        }
        case CURLOPT_XFERINFOFUNCTION:
            if(s->callbacks)stop();
            result=curl_easy_setopt(e->native,CURLOPT_XFERINFODATA,e);
            if(result==CURLE_OK)result=curl_easy_setopt(e->native,CURLOPT_XFERINFOFUNCTION,value?progress_callback:NULL);
            if(result==CURLE_OK)e->progress_function=value;break;
        case CURLOPT_XFERINFODATA:
            result=curl_easy_setopt(e->native,CURLOPT_XFERINFODATA,e);
            if(result==CURLE_OK)e->progress_data=value;break;
        case CURLOPT_NOPROGRESS:result=curl_easy_setopt(e->native,CURLOPT_NOPROGRESS,(long)(int32_t)value);break;
        case CURLOPT_FOLLOWLOCATION:result=curl_easy_setopt(e->native,CURLOPT_FOLLOWLOCATION,(long)(int32_t)value);break;
        case CURLOPT_CURLU: {
            if(e->attached||s->callbacks)stop();
            Url *u=value?*url_slot(s,value):NULL;
            result=curl_easy_setopt(e->native,CURLOPT_CURLU,u?u->native:NULL);
            if(result==CURLE_OK){e->url=value;e->stale_url=0;}break;
        }
        case CURLOPT_HTTPHEADER: {
            if(e->attached||s->callbacks)stop();
            StringList *list=value?*list_slot(s,value):NULL;
            result=curl_easy_setopt(e->native,CURLOPT_HTTPHEADER,list?list->head->native:NULL);
            if(result==CURLE_OK){e->headers=value;e->stale_headers=0;}break;
        }
        case CURLOPT_URL:result=curl_easy_setopt(e->native,CURLOPT_URL,text);break;
        case CURLOPT_NOPROXY:result=curl_easy_setopt(e->native,CURLOPT_NOPROXY,text);break;
        case CURLOPT_CAINFO:result=curl_easy_setopt(e->native,CURLOPT_CAINFO,text?ca_path:NULL);break;
        case CURLOPT_USERAGENT:result=curl_easy_setopt(e->native,CURLOPT_USERAGENT,text);break;
        case CURLOPT_IPRESOLVE:result=curl_easy_setopt(e->native,CURLOPT_IPRESOLVE,(long)(int32_t)value);break;
        case CURLOPT_TIMEOUT:result=curl_easy_setopt(e->native,CURLOPT_TIMEOUT,(long)(int32_t)value);break;
        case CURLOPT_TIMECONDITION:result=curl_easy_setopt(e->native,CURLOPT_TIMECONDITION,(long)(int32_t)value);break;
        case CURLOPT_FORBID_REUSE:result=curl_easy_setopt(e->native,CURLOPT_FORBID_REUSE,(long)(int32_t)value);break;
        case CURLOPT_SSL_OPTIONS:result=curl_easy_setopt(e->native,CURLOPT_SSL_OPTIONS,(long)(int32_t)value);break;
        case CURLOPT_NOSIGNAL:
            if(value!=1)stop();
            result=curl_easy_setopt(e->native,CURLOPT_NOSIGNAL,1L);break;
        case CURLOPT_TIMEVALUE_LARGE: {
            /* This x86 vararg alone occupies two stack words. Do not read a
               nonexistent fourth argument for ordinary 32-bit selectors. */
            if(cpu->_esp>UINT32_MAX-19)stop();uint32_t high;
            memcpy(&high,guest(s,cpu->_esp+16,4),4);
            uint64_t bits=((uint64_t)high<<32)|value;int64_t signed_value;memcpy(&signed_value,&bits,8);
            result=curl_easy_setopt(e->native,CURLOPT_TIMEVALUE_LARGE,(curl_off_t)signed_value);break;
        }
        case CURLOPT_WRITEFUNCTION:
            if(!value&&e->write_data)stop(); /* Guest FILE* streams require a separate bridge. */
            result=curl_easy_setopt(e->native,CURLOPT_WRITEDATA,value?(void *)e:(void *)stdout);
            if(result==CURLE_OK)result=curl_easy_setopt(e->native,CURLOPT_WRITEFUNCTION,value?write_callback:NULL);
            if(result==CURLE_OK)e->write_function=value;
            break;
        case CURLOPT_WRITEDATA:
            result=CURLE_OK;
            if(e->write_function)result=curl_easy_setopt(e->native,CURLOPT_WRITEDATA,e);
            if(result==CURLE_OK)e->write_data=value;
            break;
        case CURLOPT_TIMEOUT_MS:result=curl_easy_setopt(e->native,CURLOPT_TIMEOUT_MS,(long)(int32_t)value);break;
        /* HTTP POST: hosting a server registers it with the TribesNEXT master
           list. The body is copied when it is set (COPYPOSTFIELDS), so the
           transfer never reads guest memory later. */
        case CURLOPT_POST:result=curl_easy_setopt(e->native,CURLOPT_POST,(long)(int32_t)value);break;
        case CURLOPT_POSTFIELDSIZE:
            e->post_size=(int32_t)value;result=curl_easy_setopt(e->native,CURLOPT_POSTFIELDSIZE,(long)(int32_t)value);break;
        case CURLOPT_POSTFIELDS:
        case CURLOPT_COPYPOSTFIELDS: {
            if(!value){result=curl_easy_setopt(e->native,CURLOPT_COPYPOSTFIELDS,NULL);break;}
            const char *source=guest(s,value,1);size_t limit=UINT64_C(0x100000000)-value;
            size_t length=e->post_size>=0?(size_t)e->post_size:strnlen(source,limit);
            if(length>=limit)stop();
            void *body=malloc(length+1);if(!body){result=CURLE_OUT_OF_MEMORY;break;}
            uint32_t token=tribes_memory_access_begin((void *)source,length?length:1,1);memcpy(body,source,length);tribes_memory_access_end(token);
            ((char *)body)[length]=0;
            result=curl_easy_setopt(e->native,CURLOPT_POSTFIELDSIZE,(long)length);
            if(result==CURLE_OK)result=curl_easy_setopt(e->native,CURLOPT_COPYPOSTFIELDS,body);
            free(body);break;
        }
        case CURLOPT_PRIVATE:
            result=curl_easy_setopt(e->native,CURLOPT_PRIVATE,value?e:NULL);
            if(result==CURLE_OK)e->private=value;
            break;
        default:fprintf(stderr,"TribesPad: unsupported curl easy option %u\n",option);abort();
    }
    leave(s,&frame);free(text);return (uint32_t)result;
}
/* Keep this selector literal at the native variadic boundary: optimization
   otherwise merges it with RESPONSE_CODE into a dynamic selector. */
static __attribute__((noinline)) CURLcode read_os_errno(CURL *native,long *value){
    return curl_easy_getinfo(native,CURLINFO_OS_ERRNO,value);
}
uint32_t tribes_curl_easy_getinfo(_cpu *cpu,uint32_t handle,uint32_t info,uint32_t output) {
    CURL_SCOPE=get(cpu,1);Easy *e=easy(s,handle);if(!e)return CURLE_BAD_FUNCTION_ARGUMENT;
    void *destination=guest(s,output,4);uint32_t value=0;
    TribesCurlFrame frame;enter(s,cpu,&frame);CURLcode result;
    if(e->borrowed&&tribes_curl_allocator_address(e->native)!=e->guest)stop();
    if(info==CURLINFO_PRIVATE) {
        char *pointer=NULL;result=curl_easy_getinfo(e->native,CURLINFO_PRIVATE,&pointer);
        if(result==CURLE_OK){if(pointer!=(e->private?(char *)e:NULL))stop();value=e->private;}
    } else if(info==CURLINFO_RESPONSE_CODE||info==CURLINFO_OS_ERRNO) {
        long code=0;
        if(info==CURLINFO_RESPONSE_CODE)result=curl_easy_getinfo(e->native,CURLINFO_RESPONSE_CODE,&code);
        else result=read_os_errno(e->native,&code);
        if(code<INT32_MIN||code>INT32_MAX)stop();value=(uint32_t)(int32_t)code;
    } else if(info==CURLINFO_CAINFO) {
        char *native=NULL;result=curl_easy_getinfo(e->native,CURLINFO_CAINFO,&native);
        if(result==CURLE_OK&&native){
            size_t n=strlen(native)+1;if(n>UINT32_MAX)stop();
            if(!e->ca_info)e->ca_info=tribes_global_alloc(s->base,0,(uint32_t)n);
            if(!e->ca_info)result=CURLE_OUT_OF_MEMORY;
            else {if(tribes_global_size(s->base,e->ca_info)<n)stop();memcpy(guest(s,e->ca_info,(uint32_t)n),native,n);value=e->ca_info;}
        }
    } else {fprintf(stderr,"TribesPad: unsupported curl info %u\n",info);abort();}
    leave(s,&frame);if(result==CURLE_OK)memcpy(destination,&value,4);return (uint32_t)result;
}

uint32_t tribes_curl_easy_perform(_cpu *cpu,uint32_t handle) {
    CURL_SCOPE=get(cpu,1);Easy *e=easy(s,handle);if(!e||e->borrowed)return CURLE_BAD_FUNCTION_ARGUMENT;
    if(e->stale_headers||e->stale_url||(!e->write_function&&e->write_data)||(!e->header_function&&e->header_data&&!e->write_function))stop();
    TribesCurlFrame frame;enter(s,cpu,&frame);
    CURLcode result=curl_easy_perform(e->native);
    leave(s,&frame);return (uint32_t)result;
}
