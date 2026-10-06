#ifndef TRIBESPAD_GUEST_CURL_ALLOCATOR_H
#define TRIBESPAD_GUEST_CURL_ALLOCATOR_H
#include "guest_callback.h"
#include <stddef.h>
typedef struct TribesCurlAllocator TribesCurlAllocator;
typedef struct TribesCurlFrame {
    TribesCurlAllocator *allocator;
    _cpu *cpu;
    struct TribesCurlFrame *previous;
    unsigned suspended;
} TribesCurlFrame;
/* Callback order matches curl_global_init_mem: malloc, free, realloc,
   strdup, calloc. Owner-thread, synchronous-resolver profile. The selected
   QoL realloc retains the old allocation on NULL, including size zero. */
TribesCurlAllocator *tribes_curl_allocator_create(uint64_t base,const uint32_t callbacks[5]);
/* Explicit opt-in for curl: exclusive outer frames may hand off between
   threads. Registry/lifetime ownership remains the caller's responsibility.
   Native callbacks still require the current thread's valid guest CPU frame. */
TribesCurlAllocator *tribes_curl_allocator_create_serialized(uint64_t base,const uint32_t callbacks[5]);
void tribes_curl_allocator_destroy(TribesCurlAllocator *allocator);
void tribes_curl_allocator_enter(TribesCurlAllocator *allocator,_cpu *cpu,TribesCurlFrame *frame);
void tribes_curl_allocator_leave(TribesCurlFrame *frame);
/* Bracket dispatch to a non-allocator guest callback. A nested native curl
   entry may occur only while its parent frame is suspended this way. */
void tribes_curl_allocator_guest_begin(TribesCurlFrame *frame);
void tribes_curl_allocator_guest_end(TribesCurlFrame *frame);
/* Exact native curl allocator signatures. A frame must cover native curl
   allocation/lifecycle calls, including cleanup. The separately reviewed
   allocator-free curl_version path needs no frame or global initialization.
   Returned pointers are private host buffers.
   Live opaque mirrors are read-only to guest consumers: changed bytes are
   rejected, never imported into host pointer-bearing structures. */
void *tribes_curl_host_malloc(size_t size);
void tribes_curl_host_free(void *pointer);
void *tribes_curl_host_realloc(void *pointer,size_t size);
char *tribes_curl_host_strdup(const char *string);
void *tribes_curl_host_calloc(size_t count,size_t size);
/* Transfer a curl-owned result buffer into guest ownership. After this call
   curl must never retain/use/free the native pointer. The guest later frees
   the returned pointer using its own allocator. Not for opaque handles. */
uint32_t tribes_curl_allocator_export(void *pointer);
size_t tribes_curl_allocator_live(TribesCurlAllocator *allocator);
/* Identity only for an opaque handle registry; never expose the host pointer
   or interpret the guest mirror as a public curl structure. */
uint32_t tribes_curl_allocator_address(void *pointer);
#endif
