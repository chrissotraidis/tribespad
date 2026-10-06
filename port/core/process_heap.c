/* x86 heaps with host-only ownership metadata and 8-byte alignment.
   Private growable heaps obtain nonexecuting segments from the existing guest
   VM backend, including its checked commitment/release path. A host registry
   lock also protects NO_SERIALIZE heaps; callers may still serialize externally.
   Exception-generating, executable and fixed-size private heaps remain gated. */
#include "process_heap.h"
#include "guest_heap.h"
#include "guest_virtual_memory.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
typedef struct Segment { uint32_t address,size;struct Segment *next; } Segment;
typedef struct Block { uint32_t address,span,requested;int used;Segment *segment;struct Block *next; } Block;
typedef struct Heap { uint64_t base;uint32_t handle;int private_heap;Block *blocks;Segment *segments;unsigned char committed[65536];struct Heap *next; } Heap;
static Heap *heaps;
static uint32_t next_handle=0xe4000001;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static void flags_check(uint32_t flags,uint32_t supported,const char *name) {
    if(flags&~supported){fprintf(stderr,"TribesPad: unsupported %s flags 0x%x\n",name,flags);abort();}
}
static Heap *find(uint64_t base,uint32_t handle) {
    for(Heap *h=heaps;h;h=h->next)if(h->base==base&&h->handle==handle)return h;
    return NULL;
}
static Heap *new_heap(uint64_t base,int private_heap) {
    Heap *h=calloc(1,sizeof *h);
    if(!h||next_handle>0xe4ffffff){free(h);return NULL;}
    h->base=base;h->handle=next_handle++;h->private_heap=private_heap;return h;
}
/* Add a reusable segment; host-page rounding works in native and checked mode.
   One MiB is a reservation growth policy, not a maximum heap/allocation size. */
static int add_segment(Heap *h,uint32_t minimum,uint32_t initial) {
    long page=sysconf(_SC_PAGESIZE);if(page<=0)return 0;
    uint64_t size=((uint64_t)minimum+page-1)&~((uint64_t)page-1);
    if(size<0x100000)size=0x100000;
    if(size>UINT32_MAX)return 0;
    Segment *s=calloc(1,sizeof *s);Block *b=calloc(1,sizeof *b);
    if(!s||!b){free(s);free(b);return 0;}
    s->address=tribes_virtual_alloc(h->base,0,(uint32_t)size,0x2000,4);s->size=(uint32_t)size;
    if(!s->address){free(s);free(b);return 0;}
    uint64_t commit=((uint64_t)initial+page-1)&~((uint64_t)page-1);
    if(commit&&!tribes_virtual_alloc(h->base,s->address,(uint32_t)commit,0x1000,4)) {
        tribes_virtual_free(h->base,s->address,0,0x8000);free(s);free(b);return 0;
    }
    s->next=h->segments;h->segments=s;
    b->address=s->address;b->span=s->size;b->segment=s;b->next=h->blocks;h->blocks=b;return 1;
}
uint32_t tribes_get_process_heap(uint64_t base) {
    pthread_mutex_lock(&lock);
    for(Heap *h=heaps;h;h=h->next)if(h->base==base&&!h->private_heap){uint32_t result=h->handle;pthread_mutex_unlock(&lock);return result;}
    Heap *h=new_heap(base,0);Block *b=calloc(1,sizeof *b);
    if(!h||!b){free(h);free(b);pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
    b->address=0x50000000;b->span=0x10000000;h->blocks=b;h->next=heaps;heaps=h;
    uint32_t result=h->handle;pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_heap_create(uint64_t base,uint32_t options,uint32_t initial,uint32_t maximum) {
    flags_check(options,1,"HeapCreate");
    if(maximum){fprintf(stderr,"TribesPad: fixed-size private heap unsupported\n");abort();}
    uint32_t saved=tribes_get_last_error();pthread_mutex_lock(&lock);Heap *h=new_heap(base,1);
    if(!h||!add_segment(h,initial,initial?initial:4096)){free(h);pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
    h->next=heaps;heaps=h;uint32_t result=h->handle;pthread_mutex_unlock(&lock);tribes_set_last_error(saved);return result;
}
static int commit_block(Heap *h,uint32_t address,uint32_t bytes) {
    long page=sysconf(_SC_PAGESIZE);if(page<=0)return 0;
    uint64_t start=address&~((uint64_t)page-1),end=((uint64_t)address+bytes+page-1)&~((uint64_t)page-1);
    if(h->private_heap)return tribes_virtual_alloc(h->base,(uint32_t)start,(uint32_t)(end-start),0x1000,4)!=0;
    if(mprotect((void *)(uintptr_t)(h->base+start),(size_t)(end-start),PROT_READ|PROT_WRITE))return 0;
    memset(h->committed+(start-0x50000000)/4096,1,(size_t)((end-start)/4096));return 1;
}
static int split(Block *b,uint32_t size) {
    if(size==b->span)return 1;
    Block *tail=calloc(1,sizeof *tail);if(!tail)return 0;
    tail->address=b->address+size;tail->span=b->span-size;tail->segment=b->segment;tail->next=b->next;b->next=tail;b->span=size;return 1;
}
static void coalesce(Heap *h) {
    for(Block *b=h->blocks;b&&b->next;) {
        Block *n=b->next;
        if(!b->used&&!n->used&&b->segment==n->segment&&(uint64_t)b->address+b->span==n->address){b->span+=n->span;b->next=n->next;free(n);}
        else b=n;
    }
}
static uint32_t alloc_locked(Heap *h,uint32_t bytes) {
    uint64_t n=((uint64_t)(bytes?bytes:1)+7)&~UINT64_C(7);
    if(!h||n>UINT32_MAX||h->base>UINTPTR_MAX-UINT64_C(0x80000000))return 0;
    Block *b=h->blocks;while(b&&(b->used||b->span<n))b=b->next;
    if(!b&&h->private_heap&&add_segment(h,(uint32_t)n,0))b=h->blocks;
    if(!b||!commit_block(h,b->address,(uint32_t)n)||!split(b,(uint32_t)n))return 0;
    b->used=1;b->requested=bytes;
    /* Zero even without ZERO_MEMORY is permitted; only this block is cleared. */
    memset((void *)(uintptr_t)(h->base+b->address),0,(size_t)n);return b->address;
}
uint32_t tribes_heap_alloc(uint64_t base,uint32_t heap,uint32_t flags,uint32_t bytes) {
    flags_check(flags,9,"HeapAlloc");uint32_t saved=tribes_get_last_error();
    pthread_mutex_lock(&lock);uint32_t result=alloc_locked(find(base,heap),bytes);pthread_mutex_unlock(&lock);
    tribes_set_last_error(saved);return result;
}
static Block *owned_block(Heap *h,uint32_t address) {
    for(Block *b=h?h->blocks:NULL;b;b=b->next)if(b->address==address&&b->used)return b;
    return NULL;
}
uint32_t tribes_heap_free(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address) {
    flags_check(flags,1,"HeapFree");pthread_mutex_lock(&lock);Heap *h=find(base,heap);Block *b=owned_block(h,address);
    if(!b){pthread_mutex_unlock(&lock);tribes_set_last_error(h?87:6);return 0;}
    b->used=0;coalesce(h);pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_heap_realloc(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address,uint32_t bytes) {
    flags_check(flags,25,"HeapReAlloc");uint32_t saved=tribes_get_last_error(),result=0;
    uint64_t n=((uint64_t)(bytes?bytes:1)+7)&~UINT64_C(7);
    pthread_mutex_lock(&lock);Heap *h=find(base,heap);Block *b=owned_block(h,address);
    if(!b||n>UINT32_MAX)goto done;
    uint32_t old=b->requested;
    if(n>b->span) {
        Block *next=b->next;
        if(next&&!next->used&&next->segment==b->segment&&(uint64_t)b->address+b->span==next->address&&(uint64_t)b->span+next->span>=n) {
            if(!commit_block(h,address,(uint32_t)n))goto done;
            /* Reuse the free block's metadata, preserving failure atomicity. */
            uint32_t extra=(uint32_t)n-b->span;
            if(extra==next->span){b->next=next->next;free(next);}
            else {next->address+=extra;next->span-=extra;}
            b->span=(uint32_t)n;
        } else {
            if(flags&16)goto done;
            result=alloc_locked(h,bytes);if(!result)goto done;
            memcpy((void *)(uintptr_t)(base+result),(void *)(uintptr_t)(base+address),old<bytes?old:bytes);
            b->used=0;coalesce(h);goto done;
        }
    } else if(n<b->span) {
        /* Keeping the larger physical block on metadata OOM is legal. */
        split(b,(uint32_t)n);coalesce(h);
    }
    if((flags&8)&&bytes>old)memset((void *)(uintptr_t)(base+address+old),0,bytes-old);
    b->requested=bytes;result=address;
 done:
    pthread_mutex_unlock(&lock);tribes_set_last_error(saved);return result;
}
uint32_t tribes_heap_destroy(uint64_t base,uint32_t heap) {
    uint32_t saved=tribes_get_last_error();pthread_mutex_lock(&lock);Heap *h=find(base,heap);
    if(!h||!h->private_heap){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
    for(Segment *s=h->segments;s;) {
        if(!tribes_virtual_free(base,s->address,0,0x8000)){fprintf(stderr,"TribesPad: owned heap segment release failed\n");abort();}
        Segment *next=s->next;free(s);s=next;
    }
    for(Block *b=h->blocks;b;){Block *next=b->next;free(b);b=next;}
    Heap **link=&heaps;while(*link!=h)link=&(*link)->next;*link=h->next;free(h);
    pthread_mutex_unlock(&lock);tribes_set_last_error(saved);return 1;
}
/* Freed default-heap blocks leave native backing committed. Private heaps are
   queried by the VM owner, including released/invalidated mappings. */
int tribes_process_heap_memory_query(uint64_t base,uint32_t address,uint32_t fields[7]) {
    if(!fields||address<0x50000000||address>=0x60000000||address%4096)return 0;
    pthread_mutex_lock(&lock);Heap *h=heaps;while(h&&(h->base!=base||h->private_heap))h=h->next;
    unsigned index=(address-0x50000000)/4096,end=index+1;int committed=h&&h->committed[index];
    while(end<65536&&(h&&h->committed[end])==committed)end++;
    uint32_t out[7]={address,0x50000000,4,(end-index)*4096u,committed?0x1000u:0x2000u,committed?4u:0u,0x20000};
    memcpy(fields,out,sizeof out);pthread_mutex_unlock(&lock);return 1;
}
