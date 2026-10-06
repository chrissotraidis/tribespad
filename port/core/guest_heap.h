#ifndef TRIBESPAD_GUEST_HEAP_H
#define TRIBESPAD_GUEST_HEAP_H
#include <stdint.h>
uint32_t tribes_global_alloc(uint64_t base,uint32_t flags,uint32_t bytes);
uint32_t tribes_global_free(uint64_t base,uint32_t address);
uint32_t tribes_global_size(uint64_t base,uint32_t address);
uint32_t tribes_get_last_error(void);
uint32_t tribes_set_last_error(uint32_t value);
/* Diagnostic visitor runs under the heap lock; it must not mutate guest allocations. */
int tribes_heap_visit(uint64_t base,int (*visitor)(uint32_t address,uint32_t span,void *context),void *context);
#endif
