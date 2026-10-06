#include "guest_curl_allocator.h"
#include "guest_memory_checks.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Block { void *host; uint32_t guest; size_t size; struct Block *next; } Block;
struct TribesCurlAllocator {
    uint64_t base;
    uint32_t callbacks[5];
    pthread_t owner;
    pthread_mutex_t gate;
    unsigned transferable;
    Block *blocks;
    unsigned frames,in_allocator;
};
static _Thread_local TribesCurlFrame *current;
static _Noreturn void stop(void) {
    fputs("TribesPad: invalid curl allocator ownership/frame\n",stderr); abort();
}
static void owner(TribesCurlAllocator *a) {
    if(!a || !pthread_equal(a->owner,pthread_self())) stop();
}
static TribesCurlAllocator *active(void) {
    if(!current) stop();
    TribesCurlAllocator *a=current->allocator; owner(a);
    if(a->in_allocator || current->suspended) stop();
    return a;
}
static void *guest(TribesCurlAllocator *a,uint32_t p,size_t size) {
    if(!p || size>UINT32_MAX || size>UINT64_C(0x100000000)-p || a->base>UINTPTR_MAX-p-size) stop();
    return (void *)(uintptr_t)(a->base+p);
}
static void copy(void *to,const void *from,size_t n) {
    uint32_t token=tribes_memory_transfer_begin(to,from,n);
    memcpy(to,from,n);
    tribes_memory_access_end(token);
}
static void synchronize(TribesCurlAllocator *a,int to_guest) {
    for(Block *b=a->blocks;b;b=b->next) {
        void *p=guest(a,b->guest,b->size);
        if(to_guest) copy(p,b->host,b->size);
        else {
            /* Curl owns these opaque bytes. Never import guest-controlled
               native pointers/function pointers from its mirror. Registered
               allocators may change their metadata, but not other live
               allocations. Public guest structures need separate marshalling. */
            unsigned char checked[4096];
            for(size_t offset=0;offset<b->size;) {
                size_t n=b->size-offset; if(n>sizeof checked) n=sizeof checked;
                copy(checked,(unsigned char *)p+offset,n);
                if(memcmp(checked,(unsigned char *)b->host+offset,n)) stop();
                offset+=n;
            }
        }
    }
}
static Block **find(TribesCurlAllocator *a,void *p) {
    Block **slot=&a->blocks;
    while(*slot && (*slot)->host!=p) slot=&(*slot)->next;
    if(!*slot) stop();
    return slot;
}
static uint32_t invoke(TribesCurlAllocator *a,unsigned index,unsigned count,const uint32_t *args,_cpu *cpu) {
    if(cpu->_esp<128) stop();
    a->in_allocator=1;
    uint32_t value=tribes_call_guest_cdecl(cpu,a->callbacks[index],count,args,cpu->_esp-128);
    a->in_allocator=0;
    return value;
}
static TribesCurlAllocator *create(uint64_t base,const uint32_t callbacks[5],unsigned transferable) {
    if(!base || base>UINTPTR_MAX-UINT64_C(0x100000000) || !callbacks) return NULL;
    for(unsigned i=0;i<5;i++) if(!callbacks[i]) return NULL;
    TribesCurlAllocator *a=calloc(1,sizeof *a);
    if(a) { a->base=base; memcpy(a->callbacks,callbacks,sizeof a->callbacks); a->owner=pthread_self(); a->transferable=transferable; a->gate=(pthread_mutex_t)PTHREAD_RECURSIVE_MUTEX_INITIALIZER; }
    return a;
}
TribesCurlAllocator *tribes_curl_allocator_create(uint64_t base,const uint32_t callbacks[5]) {return create(base,callbacks,0);}
TribesCurlAllocator *tribes_curl_allocator_create_serialized(uint64_t base,const uint32_t callbacks[5]) {return create(base,callbacks,1);}
void tribes_curl_allocator_destroy(TribesCurlAllocator *a) {
    if(!a)stop();
    if(!a->transferable)owner(a);
    if(pthread_mutex_lock(&a->gate))stop();
    if(!a->transferable)owner(a);
    if(a->blocks || a->frames || a->in_allocator) stop();
    if(pthread_mutex_unlock(&a->gate)||pthread_mutex_destroy(&a->gate))stop();free(a);
}
void tribes_curl_allocator_enter(TribesCurlAllocator *a,_cpu *cpu,TribesCurlFrame *frame) {
    /* Retain exclusive ownership until this entire frame leaves. Nested guest
       callbacks use the recursive lock on the same thread. A handoff can occur
       only between outer frames; SDL/default allocators remain fixed-owner. */
    if(!a)stop();
    if(!a->transferable)owner(a);
    if(pthread_mutex_lock(&a->gate))stop();
    if(a->transferable&&!a->frames)a->owner=pthread_self();
    owner(a);
    if(!cpu || !frame || cpu->_pointer_offset!=a->base || a->in_allocator ||
       (current && (current->allocator!=a || !current->suspended))) stop();
    for(TribesCurlFrame *f=current;f;f=f->previous) if(f==frame) stop();
    synchronize(a,0);
    *frame=(TribesCurlFrame){a,cpu,current,0}; current=frame; a->frames++;
}
void tribes_curl_allocator_leave(TribesCurlFrame *frame) {
    if(!frame || current!=frame) stop();
    TribesCurlAllocator *a=active();
    synchronize(a,1);
    current=frame->previous; a->frames--; memset(frame,0,sizeof *frame);
    if(pthread_mutex_unlock(&a->gate))stop();
}
void tribes_curl_allocator_guest_begin(TribesCurlFrame *frame) {
    if(!frame || frame!=current) stop();
    TribesCurlAllocator *a=active(); synchronize(a,1); frame->suspended=1;
}
void tribes_curl_allocator_guest_end(TribesCurlFrame *frame) {
    if(!frame || frame!=current || !frame->suspended) stop();
    TribesCurlAllocator *a=frame->allocator; owner(a);
    if(a->in_allocator) stop();
    synchronize(a,0); frame->suspended=0;
}
static Block *reserve(size_t size) {
    Block *b=calloc(1,sizeof *b);
    if(b) { b->host=malloc(size?size:1); b->size=size; if(!b->host) { free(b); b=NULL; } }
    return b;
}
static void discard(Block *b) { if(b) { free(b->host); free(b); } }
static void adopt(TribesCurlAllocator *a,Block *b,uint32_t address) {
    void *p=guest(a,address,b->size);
    /* Allocator outputs must be writable before uninstrumented curl sees the
       host mirror. No guest pointer is ever returned to that library. */
    uint32_t token=tribes_memory_access_begin(p,b->size,3); tribes_memory_access_end(token);
    for(Block *other=a->blocks;other;other=other->next)
        if(address==other->guest || ((uint64_t)address<other->guest+other->size &&
                                    (uint64_t)other->guest<address+b->size)) stop();
    b->guest=address; copy(b->host,p,b->size); b->next=a->blocks; a->blocks=b;
}
static void *allocate(unsigned operation,uint32_t count,uint32_t size) {
    TribesCurlAllocator *a=active();
    size_t length=(size_t)count*size;
    Block *b=reserve(length); if(!b) return NULL;
    synchronize(a,1);
    uint32_t args[]={count,size};
    uint32_t p=invoke(a,operation,operation==4?2:1,operation==4?args:&args[1],current->cpu);
    synchronize(a,0);
    if(!p) { discard(b); return NULL; }
    adopt(a,b,p);
    if(operation==4) for(size_t i=0;i<length;i++) if(((unsigned char *)b->host)[i]) stop();
    return b->host;
}
void *tribes_curl_host_malloc(size_t size) {
    (void)active(); if(size>UINT32_MAX) return NULL;
    return allocate(0,1,(uint32_t)size);
}
void *tribes_curl_host_calloc(size_t count,size_t size) {
    (void)active();
    if(count>UINT32_MAX || size>UINT32_MAX || (size && count>UINT32_MAX/size)) return NULL;
    return allocate(4,(uint32_t)count,(uint32_t)size);
}
void tribes_curl_host_free(void *p) {
    TribesCurlAllocator *a=active(); synchronize(a,1);
    Block *b=NULL; uint32_t address=0;
    if(p) { Block **slot=find(a,p); b=*slot; address=b->guest; *slot=b->next; }
    (void)invoke(a,1,1,&address,current->cpu);
    synchronize(a,0); discard(b);
}
void *tribes_curl_host_realloc(void *p,size_t size) {
    TribesCurlAllocator *a=active(); if(size>UINT32_MAX) return NULL;
    Block *replacement=reserve(size); if(!replacement) return NULL;
    synchronize(a,1);
    Block *old=NULL; uint32_t address=0;
    if(p) { Block **slot=find(a,p); old=*slot; address=old->guest; *slot=old->next; }
    uint32_t args[]={address,(uint32_t)size};
    uint32_t result=invoke(a,2,2,args,current->cpu);
    /* Selected QoL allocator allocates before freeing, including size zero.
       A NULL result therefore leaves the prior guest block alive. */
    if(!result && old) { old->next=a->blocks; a->blocks=old; }
    synchronize(a,0);
    if(!result) { discard(replacement); return NULL; }
    adopt(a,replacement,result);
    if(old && memcmp(old->host,replacement->host,size<old->size?size:old->size)) stop();
    discard(old); return replacement->host;
}
char *tribes_curl_host_strdup(const char *string) {
    TribesCurlAllocator *a=active(); if(!string) stop();
    if((uintptr_t)string>=a->base && (uintptr_t)string-a->base<UINT64_C(0x100000000)) stop();
    size_t length=strlen(string)+1; if(length>UINT32_MAX) return NULL;
    Block *b=reserve(length); if(!b) return NULL;
    _cpu *cpu=current->cpu;
    if(!cpu->stack_bottom || !cpu->stack_top || (uintptr_t)cpu->stack_bottom<a->base ||
       a->base+cpu->_esp<(uintptr_t)cpu->stack_bottom+256 ||
       a->base+cpu->_esp>(uintptr_t)cpu->stack_top) stop();
    if(length>a->base+cpu->_esp-(uintptr_t)cpu->stack_bottom-256) {
        /* Large strings must not consume the guest stack. A temporary guest
           allocation stages the input; the registered strdup still owns the
           actual result. Both temporary callbacks use the same cdecl ABI. */
        uint32_t size=(uint32_t)length;
        synchronize(a,1);
        uint32_t staging=invoke(a,0,1,&size,cpu);
        synchronize(a,0);
        if(!staging) { discard(b); return NULL; }
        copy(guest(a,staging,length),string,length);
        synchronize(a,1);
        uint32_t result=invoke(a,3,1,&staging,cpu);
        synchronize(a,0);
        if(result==staging) stop();
        if(result) { adopt(a,b,result); if(memcmp(b->host,string,length)) stop(); }
        synchronize(a,1);
        (void)invoke(a,1,1,&staging,cpu);
        synchronize(a,0);
        if(!result) { discard(b); return NULL; }
        return b->host;
    }
    /* Stage the native string below the caller's stack. The callback stack is
       placed below the staged input, with the usual 128-byte separation. */
    uint32_t address=(cpu->_esp-128-(uint32_t)length)&~15u;
    if(a->base+address<(uintptr_t)cpu->stack_bottom+128 ||
       a->base+cpu->_esp>(uintptr_t)cpu->stack_top) stop();
    void *staging=guest(a,address,length);
    copy(staging,string,length);
    synchronize(a,1);
    uint32_t saved_sp=cpu->_esp; cpu->_esp=address;
    uint32_t result=invoke(a,3,1,&address,cpu); cpu->_esp=saved_sp;
    synchronize(a,0);
    if(!result) { discard(b); return NULL; }
    adopt(a,b,result); if(memcmp(b->host,string,length)) stop(); return b->host;
}
uint32_t tribes_curl_allocator_export(void *pointer) {
    TribesCurlAllocator *a=active(); Block **slot=find(a,pointer); Block *b=*slot;
    copy(guest(a,b->guest,b->size),b->host,b->size);
    uint32_t result=b->guest; *slot=b->next; discard(b); return result;
}
size_t tribes_curl_allocator_live(TribesCurlAllocator *a) {
    if(!a)stop();
    if(!a->transferable)owner(a);
    if(pthread_mutex_lock(&a->gate))stop();
    if(!a->transferable)owner(a);
    size_t count=0; for(Block *b=a->blocks;b;b=b->next) count++;
    if(pthread_mutex_unlock(&a->gate))stop();return count;
}
uint32_t tribes_curl_allocator_address(void *pointer) {
    return (*find(active(),pointer))->guest;
}
