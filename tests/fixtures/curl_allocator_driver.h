#include "../../port/core/guest_curl_allocator.h"
#include "../../port/core/guest_memory_checks.h"
extern pthread_t owner;
extern size_t live,peak,counts[5];
extern int fail_next;
extern void *allocations;
void owner_check(void);
void curl_allocator_test_setup(void);
void curl_allocator_test_finish(void);
void curl_allocator_test_fault(const char *name);
void *curl_allocator_test_thread(void *unused);
uint32_t curl_allocator_test_prepare_protected(const char *name);
#define memory_malloc tribes_curl_host_malloc
#define memory_free tribes_curl_host_free
#define memory_realloc tribes_curl_host_realloc
#define memory_strdup tribes_curl_host_strdup
#define memory_calloc tribes_curl_host_calloc
