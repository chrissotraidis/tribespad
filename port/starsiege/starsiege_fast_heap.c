/* Fast Win32 heaps for Starsiege: Tribes. Its MSVC 2005 C runtime sends every
 * malloc/free to HeapAlloc/HeapFree, so the heap sees tens of thousands of
 * small blocks. TribesPad's heap (process_heap.c) walks one list per call,
 * which made start-up stall for about 25 seconds. Here: size classes with
 * O(1) free lists over 1 MiB chunks of guest virtual memory, dedicated
 * regions for blocks over 256 KiB, and a host-side hash table from block
 * address to size for HeapSize/HeapFree/HeapReAlloc. Blocks are 16-byte
 * aligned (Windows guarantees 8). The process heap keeps TribesPad's handle;
 * a block this heap did not allocate (the runtime's own) goes to
 * process_heap.c, so mixed frees stay correct. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/guest_memory_checks.h"

extern uint32_t tribes_set_last_error(uint32_t value);
extern uint32_t tribes_virtual_alloc(uint64_t base,uint32_t address,uint32_t size,uint32_t type,uint32_t protection);
extern uint32_t tribes_virtual_free(uint64_t base,uint32_t address,uint32_t size,uint32_t type);
extern uint32_t tribes_get_process_heap(uint64_t base);
extern uint32_t tribes_heap_alloc(uint64_t base,uint32_t heap,uint32_t flags,uint32_t bytes);
extern uint32_t tribes_heap_free(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address);
extern uint32_t tribes_heap_realloc(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address,uint32_t bytes);
extern uint32_t ss_heap_size(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address);

#define SMALL_CLASSES 64          /* 16..1024 in 16-byte steps */
#define LARGE_CLASSES 8           /* 2 KiB..256 KiB, powers of two */
#define CLASSES (SMALL_CLASSES+LARGE_CLASSES)
#define CHUNK (1u<<20)
#define HUGE_CLASS 0xff

typedef struct {uint32_t *items,count,capacity;} List;
typedef struct Heap {uint64_t base;uint32_t handle;uint32_t chunk,used;List free[CLASSES];List regions;struct Heap *next;} Heap;
typedef struct {uint32_t address,size,heap;uint8_t cls;} Entry;
static Heap *heaps;
static Entry *table;static uint32_t table_capacity,table_count;
static uint32_t next_handle=0xe5800001u;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;

static _Noreturn void fail(const char *what,uint32_t value){fprintf(stderr,"TribesPad: Starsiege heap: %s 0x%x\n",what,value);abort();}
static void push(List *l,uint32_t v){
 if(l->count==l->capacity){uint32_t n=l->capacity?l->capacity*2:64;uint32_t *p=realloc(l->items,n*sizeof *p);if(!p)fail("host memory",n);l->items=p;l->capacity=n;}
 l->items[l->count++]=v;
}
static unsigned class_of(uint32_t bytes,uint32_t *capacity){
 if(bytes<=1024){unsigned c=(bytes+15)/16;if(!c)c=1;*capacity=c*16;return c-1;}
 uint32_t size=2048;for(unsigned c=0;c<LARGE_CLASSES;c++,size<<=1)if(bytes<=size){*capacity=size;return SMALL_CLASSES+c;}
 *capacity=(bytes+0xffffu)&~0xffffu;return HUGE_CLASS;
}
static uint32_t capacity_of(uint8_t cls,uint32_t size){
 if(cls==HUGE_CLASS)return (size+0xffffu)&~0xffffu;
 return cls<SMALL_CLASSES?(cls+1u)*16:2048u<<(cls-SMALL_CLASSES);
}
static uint32_t hash(uint32_t a){a^=a>>16;a*=0x7feb352du;a^=a>>15;a*=0x846ca68bu;a^=a>>16;return a;}
static Entry *lookup(uint32_t address){
 if(!table_capacity)return NULL;uint32_t mask=table_capacity-1;
 for(uint32_t i=hash(address)&mask;;i=(i+1)&mask){if(!table[i].address)return NULL;if(table[i].address==address)return &table[i];}
}
static void insert(Entry e);
static void grow(void){
 Entry *old=table;uint32_t n=table_capacity;table_capacity=n?n*2:1u<<16;table=calloc(table_capacity,sizeof *table);if(!table)fail("host memory",table_capacity);
 table_count=0;for(uint32_t i=0;i<n;i++)if(old[i].address)insert(old[i]);free(old);
}
static void insert(Entry e){
 if((table_count+1)*4>=table_capacity*3)grow();uint32_t mask=table_capacity-1;
 for(uint32_t i=hash(e.address)&mask;;i=(i+1)&mask)if(!table[i].address){table[i]=e;table_count++;return;}
}
static void erase(Entry *e){
 uint32_t mask=table_capacity-1,i=(uint32_t)(e-table);table[i].address=0;table_count--;
 for(uint32_t j=(i+1)&mask;table[j].address;j=(j+1)&mask){Entry moved=table[j];table[j].address=0;table_count--;insert(moved);}
}
static Heap *find(uint64_t base,uint32_t handle){for(Heap *h=heaps;h;h=h->next)if(h->base==base&&h->handle==handle)return h;return NULL;}
static Heap *ours(uint64_t base,uint32_t handle){
 Heap *h=find(base,handle);if(h)return h;
 if(handle!=tribes_get_process_heap(base))return NULL;
 h=calloc(1,sizeof *h);if(!h)fail("host memory",0);h->base=base;h->handle=handle;h->next=heaps;heaps=h;return h;
}
static uint32_t carve(Heap *h,uint32_t bytes){
 if(!h->chunk||h->used+bytes>CHUNK){
  uint32_t chunk=tribes_virtual_alloc(h->base,0,CHUNK,0x3000,4);if(!chunk)return 0;
  push(&h->regions,chunk);h->chunk=chunk;h->used=0;
 }
 uint32_t a=h->chunk+h->used;h->used+=bytes;return a;
}
static uint32_t allocate(Heap *h,uint32_t bytes,uint32_t flags){
 uint32_t capacity;unsigned cls=class_of(bytes,&capacity);uint32_t a=0;
 if(cls==HUGE_CLASS){a=tribes_virtual_alloc(h->base,0,capacity,0x3000,4);if(a)push(&h->regions,a);}
 else if(h->free[cls].count)a=h->free[cls].items[--h->free[cls].count];
 else a=carve(h,capacity);
 if(!a){tribes_set_last_error(8);if(flags&4)fail("allocation failure with HEAP_GENERATE_EXCEPTIONS",bytes);return 0;}
 insert((Entry){a,bytes,h->handle,(uint8_t)cls});
 if(flags&8){void *p=(void *)(uintptr_t)(h->base+a);uint32_t t=tribes_memory_access_begin(p,bytes?bytes:1,2);memset(p,0,bytes);tribes_memory_access_end(t);}
 return a;
}
static void release(Heap *h,Entry *e){
 uint32_t a=e->address,size=e->size;uint8_t cls=e->cls;erase(e);
 if(cls==HUGE_CLASS){tribes_virtual_free(h->base,a,0,0x8000);for(uint32_t i=0;i<h->regions.count;i++)if(h->regions.items[i]==a){h->regions.items[i]=h->regions.items[--h->regions.count];break;}(void)size;}
 else push(&h->free[cls],a);
}

uint32_t ss_get_process_heap(uint64_t base){return tribes_get_process_heap(base);}
uint32_t ss_heap_create(uint64_t base,uint32_t options,uint32_t initial,uint32_t maximum){
 (void)initial;if(options&~(uint32_t)(1|4)||maximum)fail("HeapCreate options/maximum",options|maximum);
 pthread_mutex_lock(&lock);Heap *h=calloc(1,sizeof *h);if(!h)fail("host memory",0);h->base=base;h->handle=next_handle++;h->next=heaps;heaps=h;uint32_t r=h->handle;pthread_mutex_unlock(&lock);return r;
}
uint32_t ss_heap_destroy(uint64_t base,uint32_t handle){
 pthread_mutex_lock(&lock);Heap **p=&heaps;while(*p&&((*p)->base!=base||(*p)->handle!=handle))p=&(*p)->next;
 Heap *h=*p;if(!h||handle==tribes_get_process_heap(base)){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
 *p=h->next;
 for(uint32_t i=0;i<table_capacity;)if(table[i].address&&table[i].heap==handle)erase(&table[i]);else i++;
 for(uint32_t i=0;i<h->regions.count;i++)tribes_virtual_free(base,h->regions.items[i],0,0x8000);
 for(unsigned c=0;c<CLASSES;c++)free(h->free[c].items);free(h->regions.items);free(h);pthread_mutex_unlock(&lock);return 1;
}
uint32_t ss_heap_alloc(uint64_t base,uint32_t handle,uint32_t flags,uint32_t bytes){
 if(flags&~(uint32_t)(1|4|8))fail("HeapAlloc flags",flags);
 pthread_mutex_lock(&lock);Heap *h=ours(base,handle);
 if(!h){pthread_mutex_unlock(&lock);return tribes_heap_alloc(base,handle,flags&9,bytes);}
 uint32_t a=allocate(h,bytes,flags);pthread_mutex_unlock(&lock);return a;
}
uint32_t ss_heap_free(uint64_t base,uint32_t handle,uint32_t flags,uint32_t address){
 if(!address)return 1;
 pthread_mutex_lock(&lock);Entry *e=lookup(address);Heap *h=e?find(base,e->heap):NULL;
 if(!e||!h){pthread_mutex_unlock(&lock);return tribes_heap_free(base,handle,flags&1,address);}
 release(h,e);pthread_mutex_unlock(&lock);return 1;
}
uint32_t ss_heap_size_fast(uint64_t base,uint32_t handle,uint32_t flags,uint32_t address){
 pthread_mutex_lock(&lock);Entry *e=lookup(address);uint32_t size=e?e->size:0;int found=e!=NULL;pthread_mutex_unlock(&lock);
 return found?size:ss_heap_size(base,handle,flags,address);
}
uint32_t ss_heap_realloc(uint64_t base,uint32_t handle,uint32_t flags,uint32_t address,uint32_t bytes){
 if(flags&~(uint32_t)(1|4|8|0x10))fail("HeapReAlloc flags",flags);
 pthread_mutex_lock(&lock);Entry *e=lookup(address);Heap *h=e?find(base,e->heap):NULL;
 if(!e||!h){pthread_mutex_unlock(&lock);if(flags&0x10)fail("HeapReAlloc in place on a runtime block",address);return tribes_heap_realloc(base,handle,flags&9,address,bytes);}
 uint32_t old=e->size,capacity=capacity_of(e->cls,old);
 if(bytes<=capacity&&(e->cls==HUGE_CLASS||bytes>capacity/2||e->cls==0)){
  e->size=bytes;
  if((flags&8)&&bytes>old){void *p=(void *)(uintptr_t)(base+address+old);uint32_t t=tribes_memory_access_begin(p,bytes-old,2);memset(p,0,bytes-old);tribes_memory_access_end(t);}
  pthread_mutex_unlock(&lock);return address;
 }
 if(flags&0x10){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
 uint32_t moved=allocate(h,bytes,flags&~8u);
 if(!moved){pthread_mutex_unlock(&lock);return 0;}
 uint32_t copy=old<bytes?old:bytes;void *dst=(void *)(uintptr_t)(base+moved),*src=(void *)(uintptr_t)(base+address);
 uint32_t t=tribes_memory_transfer_begin(dst,src,copy?copy:1);memmove(dst,src,copy);tribes_memory_access_end(t);
 if((flags&8)&&bytes>old){void *p=(void *)(uintptr_t)(base+moved+old);uint32_t z=tribes_memory_access_begin(p,bytes-old,2);memset(p,0,bytes-old);tribes_memory_access_end(z);}
 release(h,lookup(address));pthread_mutex_unlock(&lock);return moved;
}

