/* Registered guest malloc callback backed by the standalone test heap arena. */
#include "../port/core/guest_heap.h"
uint32_t test_malloc(uint64_t base,uint32_t size){
    return tribes_global_alloc(base,0,size);
}
