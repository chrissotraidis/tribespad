/* Fixed GlobalAlloc memory in the reserved, nonexecutable guest address window.
   Host metadata is never guest writable. Movable handles remain an explicit gate.
   Contract: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalalloc */
#include "guest_heap.h"
#include "guest_memory_layout.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
typedef struct Block { uint32_t address,span;int used;struct Block *next; } Block;
typedef struct Heap { uint64_t base;Block *blocks;struct Heap *next; } Heap;
static Heap *heaps;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local uint32_t last_error;
uint32_t tribes_global_heap_span=0x20000000;
uint32_t tribes_get_last_error(void) { return last_error; }
uint32_t tribes_set_last_error(uint32_t value) { last_error=value;return 0; }
static Heap *find_heap(uint64_t base,int create) {
    for(Heap *h=heaps;h;h=h->next)if(h->base==base)return h;
    if(!create)return NULL;
    Heap *h=calloc(1,sizeof(*h));Block *b=calloc(1,sizeof(*b));
    if(!h||!b){free(h);free(b);return NULL;}
    b->address=0x30000000;b->span=tribes_global_heap_span;
    h->base=base;h->blocks=b;h->next=heaps;heaps=h;return h;
}
uint32_t tribes_global_alloc(uint64_t base,uint32_t flags,uint32_t bytes) {
    if(flags&~UINT32_C(0x40)) { fprintf(stderr,"TribesPad: unsupported GlobalAlloc flags 0x%x\n",flags);abort(); }
    long host_page=sysconf(_SC_PAGESIZE);if(host_page<=0)return 0;
    uint64_t page=(uint64_t)host_page,n=((bytes?bytes:1)+(page-1))&~(page-1);
    if(n>tribes_global_heap_span || base>UINTPTR_MAX-UINT32_C(0x50000000)){last_error=8;return 0;}
    pthread_mutex_lock(&lock);Heap *h=find_heap(base,1);Block *b=h?h->blocks:NULL;
    while(b&&(b->used||b->span<n))b=b->next;
    if(!b){pthread_mutex_unlock(&lock);last_error=8;return 0;}
    Block *tail=NULL;
    if(b->span>n) {
        tail=calloc(1,sizeof(*tail));if(!tail){pthread_mutex_unlock(&lock);last_error=8;return 0;}
        tail->address=b->address+(uint32_t)n;tail->span=b->span-(uint32_t)n;tail->next=b->next;
    }
    void *host=(void *)(uintptr_t)(base+b->address);
    if(mprotect(host,(size_t)n,PROT_READ|PROT_WRITE)!=0) { free(tail);pthread_mutex_unlock(&lock);last_error=8;return 0; }
    b->span=(uint32_t)n;b->used=1;if(tail)b->next=tail;
    /* Define deterministic fresh contents even after Darwin MADV_DONTNEED,
       which does not promise immediate page discard. Flags 0 leaves contents
       unspecified in Win32; zero fill is a valid provider policy for both modes. */
    { unsigned char *p=host;for(uint64_t i=0;i<n;i++)p[i]=0; }
    uint32_t result=b->address;pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_global_free(uint64_t base,uint32_t address) {
    if(!address)return 0;
    pthread_mutex_lock(&lock);Heap *h=find_heap(base,0);Block *b=h?h->blocks:NULL,*previous=NULL;
    while(b&&b->address!=address){previous=b;b=b->next;}
    if(!b||!b->used){pthread_mutex_unlock(&lock);last_error=6;return address;}
    void *host=(void *)(uintptr_t)(base+address);
    if(mprotect(host,b->span,PROT_NONE)!=0){pthread_mutex_unlock(&lock);last_error=6;return address;}
    (void)madvise(host,b->span,MADV_DONTNEED);b->used=0;
    if(b->next&&!b->next->used) { Block *next=b->next;b->span+=next->span;b->next=next->next;free(next); }
    if(previous&&!previous->used) { previous->span+=b->span;previous->next=b->next;free(b); }
    pthread_mutex_unlock(&lock);return 0;
}
uint32_t tribes_global_size(uint64_t base,uint32_t address) {
    pthread_mutex_lock(&lock);Heap *h=find_heap(base,0);Block *b=h?h->blocks:NULL;
    while(b&&b->address!=address)b=b->next;
    uint32_t size=b&&b->used?b->span:0;pthread_mutex_unlock(&lock);
    if(!size)last_error=6;return size;
}

int tribes_heap_visit(uint64_t base,int (*visitor)(uint32_t,uint32_t,void *),void *context) {
    if(!visitor)return 0;int result=1;pthread_mutex_lock(&lock);Heap *heap=find_heap(base,0);
    for(Block *block=heap?heap->blocks:NULL;block;block=block->next)if(block->used&&!visitor(block->address,block->span,context)){result=0;break;}
    pthread_mutex_unlock(&lock);return result;
}
