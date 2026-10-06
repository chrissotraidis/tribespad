#ifndef TRIBESPAD_PROCESS_HEAP_H
#define TRIBESPAD_PROCESS_HEAP_H
#include <stdint.h>
/* Caller owns a page-aligned 4 GiB guest window. Default heap uses
   0x50000000..0x60000000, distinct from GlobalAlloc and DirectInput objects. */
uint32_t tribes_heap_create(uint64_t base,uint32_t options,uint32_t initial,uint32_t maximum);
uint32_t tribes_heap_destroy(uint64_t base,uint32_t heap);
uint32_t tribes_heap_realloc(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address,uint32_t bytes);
uint32_t tribes_get_process_heap(uint64_t base);
uint32_t tribes_heap_alloc(uint64_t base,uint32_t heap,uint32_t flags,uint32_t bytes);
uint32_t tribes_heap_free(uint64_t base,uint32_t heap,uint32_t flags,uint32_t address);
int tribes_process_heap_memory_query(uint64_t base,uint32_t address,uint32_t fields[7]);
#endif
