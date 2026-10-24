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
/* BOOL HeapValidate(HANDLE, DWORD, LPCVOID): the heap exists, and the block
   (when given) is an allocated block of it. */
uint32_t ss_heap_validate(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address){
 flags_check(flags,1,"HeapValidate");pthread_mutex_lock(&lock);
 Heap *h=find(base,heap);uint32_t valid=h&&(!address||owned_block(h,address));
 pthread_mutex_unlock(&lock);return valid;
}

