#ifndef TRIBESPAD_NATIVE_FLS_H
#define TRIBESPAD_NATIVE_FLS_H
#include <stdint.h>
#define TRIBES_FLS_CAPACITY 128
typedef struct TribesFlsFiber TribesFlsFiber;
typedef void (*TribesFlsDispatch)(void *context,uint32_t callback,uint32_t value);
typedef void (*TribesFlsDispose)(void *context);
/* Storage contexts, not an execution scheduler. Owned by the creating native
   thread; Win32 fiber execution and cross-thread migration are not provided. */
TribesFlsFiber *tribes_fls_fiber_create(uint64_t base,TribesFlsDispatch dispatch,void *context,TribesFlsDispose dispose);
int tribes_fls_fiber_select(TribesFlsFiber *fiber);
int tribes_fls_fiber_delete(TribesFlsFiber *fiber);
void *tribes_fls_current_context(uint64_t base);
void tribes_fls_thread_cleanup(uint64_t base);
/* Forced process termination discards worker values without guest callbacks. */
void tribes_fls_thread_discard(uint64_t base);
uint32_t tribes_fls_alloc_index(uint64_t base,uint32_t callback);
uint32_t tribes_fls_get_value(uint64_t base,uint32_t index);
uint32_t tribes_fls_set_value(uint64_t base,uint32_t index,uint32_t value);
uint32_t tribes_fls_free_index(uint64_t base,uint32_t index,TribesFlsDispatch dispatch,void *context);
#endif
