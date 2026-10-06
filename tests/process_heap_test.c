#include "../port/core/process_heap.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
static uint64_t base;static uint32_t heap;
static void *worker(void *arg) {
    unsigned id=(unsigned)(uintptr_t)arg;tribes_set_last_error(1000+id);
    for(unsigned i=0;i<1000;i++) {
        uint32_t bytes=31+i%257,p=tribes_heap_alloc(base,heap,8,bytes);assert(p&&!(p&7));
        unsigned char *host=(void *)(uintptr_t)(base+p);for(unsigned n=0;n<bytes;n++)assert(!host[n]);
        memset(host,id,bytes);for(unsigned n=0;n<bytes;n++)assert(host[n]==id);
        assert(tribes_heap_free(base,heap,0,p));assert(tribes_get_last_error()==1000+id);
    }
    return NULL;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);base=(uintptr_t)window;
    tribes_set_last_error(123);heap=tribes_get_process_heap(base);assert(heap&&heap==tribes_get_process_heap(base)&&tribes_get_last_error()==123);
    uint32_t a=tribes_heap_alloc(base,heap,0,1),b=tribes_heap_alloc(base,heap,8,9);assert(a==0x50000000&&b==a+8&&tribes_get_last_error()==123);
    memset((void *)(uintptr_t)(base+b),0x79,9);assert(tribes_heap_free(base,heap,0,a));a=tribes_heap_alloc(base,heap,8,1);assert(a==0x50000000);for(unsigned n=0;n<9;n++)assert(*((unsigned char *)(uintptr_t)(base+b)+n)==0x79);
    assert(!tribes_heap_alloc(base,heap,0,UINT32_MAX)&&tribes_get_last_error()==123);assert(!tribes_heap_alloc(base,heap+1,0,8)&&tribes_get_last_error()==123);
    assert(!tribes_heap_free(base,heap+1,0,b)&&tribes_get_last_error()==6);assert(!tribes_heap_free(base,heap,0,b+1)&&tribes_get_last_error()==87);
    assert(tribes_heap_free(base,heap,0,a));assert(!tribes_heap_free(base,heap,0,a));assert(tribes_heap_free(base,heap,0,b));
    uint32_t z=tribes_heap_alloc(base,heap,8,0);assert(z==0x50000000);assert(tribes_heap_free(base,heap,0,z));
    /* Small allocations really pack: 4096 live one-byte blocks use 32 KiB. */
    uint32_t live[4096];for(unsigned i=0;i<4096;i++){live[i]=tribes_heap_alloc(base,heap,8,1);assert(live[i]==0x50000000+8*i);*((unsigned char *)(uintptr_t)(base+live[i]))=(unsigned char)i;}
    for(unsigned i=0;i<4096;i++){assert(*((unsigned char *)(uintptr_t)(base+live[i]))==(unsigned char)i);assert(tribes_heap_free(base,heap,0,live[i]));}
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    uint32_t default_heap=heap;heap=tribes_heap_create(base,0,4096,0);assert(heap&&heap!=default_heap);
    for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(tribes_heap_destroy(base,heap));heap=default_heap;
    void *other=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(other!=MAP_FAILED);uint32_t other_heap=tribes_get_process_heap((uintptr_t)other);assert(other_heap!=heap);assert(!tribes_heap_alloc((uintptr_t)other,heap,0,8));z=tribes_heap_alloc((uintptr_t)other,other_heap,8,8);assert(z==0x50000000);assert(tribes_heap_free((uintptr_t)other,other_heap,0,z));assert(!munmap(other,UINT64_C(0x100000000)));assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: stable owned process heaps, 8-byte packed alignment, neighbor preservation, zero/reuse/coalescing, overflow and LastError, process isolation, 8000 concurrent lifetimes");return 0;
}
