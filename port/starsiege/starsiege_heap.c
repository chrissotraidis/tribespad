/* Starsiege's heap: TribesPad's x86 heap (port/core/process_heap.c, included
 * unchanged so its reviewed hash stays valid) plus the two queries Starsiege's
 * MSVC 2005 C runtime makes. The Starsiege build compiles this file in place
 * of process_heap.c. */
#include "../core/process_heap.c"

/* SIZE_T HeapSize(HANDLE, DWORD, LPCVOID): the size requested for the block. */
uint32_t ss_heap_size(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address){
 flags_check(flags,1,"HeapSize");pthread_mutex_lock(&lock);
 Block *b=owned_block(find(base,heap),address);uint32_t size=b?b->requested:UINT32_MAX;
 pthread_mutex_unlock(&lock);if(size==UINT32_MAX)tribes_set_last_error(87);return size;
}
/* Guest-memory allocator for SDL. TribesPad's SDL backend keeps SDL's own
   allocations in guest memory through x86 cdecl callbacks the guest registers
   (Tribes 2's client registers its C runtime). Starsiege never calls SDL, so
   scripts/starsiege/build_engine.py routes four fixed guest addresses to these
   process-heap services and the engine registers them. */
extern uint32_t ss_heap_alloc(uint64_t,uint32_t,uint32_t,uint32_t);
extern uint32_t ss_heap_free(uint64_t,uint32_t,uint32_t,uint32_t);
extern uint32_t ss_heap_realloc(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t ss_guest_malloc(uint64_t base,uint32_t size){
 return ss_heap_alloc(base,tribes_get_process_heap(base),0,size?size:1);
}
uint32_t ss_guest_calloc(uint64_t base,uint32_t count,uint32_t size){
 uint64_t bytes=(uint64_t)count*size;if(bytes>UINT32_MAX)return 0;
 uint32_t p=ss_guest_malloc(base,(uint32_t)bytes);if(p&&bytes)memset((void *)(uintptr_t)(base+p),0,(size_t)bytes);return p;
}
uint32_t ss_guest_realloc(uint64_t base,uint32_t address,uint32_t size){
 if(!address)return ss_guest_malloc(base,size);
 if(!size){ss_heap_free(base,tribes_get_process_heap(base),0,address);return 0;}
 return ss_heap_realloc(base,tribes_get_process_heap(base),0,address,size);
}
uint32_t ss_guest_free(uint64_t base,uint32_t address){
 if(address)ss_heap_free(base,tribes_get_process_heap(base),0,address);return 0;
}

/* BOOL HeapValidate(HANDLE, DWORD, LPCVOID): the heap exists, and the block
   (when given) is an allocated block of it. */
uint32_t ss_heap_validate(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address){
 flags_check(flags,1,"HeapValidate");pthread_mutex_lock(&lock);
 Heap *h=find(base,heap);uint32_t valid=h&&(!address||owned_block(h,address));
 pthread_mutex_unlock(&lock);return valid;
}

