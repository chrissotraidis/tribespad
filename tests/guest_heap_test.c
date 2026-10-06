#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static uint64_t base;
static void *worker(void *argument) {
    unsigned id=(unsigned)(uintptr_t)argument;tribes_set_last_error(1000+id);
    for(unsigned i=0;i<1000;i++) {
        uint32_t p=tribes_global_alloc(base,0x40,31+i%257);assert(p&&p%8==0);
        uint32_t size=tribes_global_size(base,p);assert(size>=31+i%257);
        unsigned char *host=(void *)(uintptr_t)(base+p);
        for(uint32_t n=0;n<size;n++)assert(host[n]==0);
        memset(host,(int)id,size);assert(!tribes_global_free(base,p));assert(tribes_get_last_error()==1000+id);
    }
    return NULL;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);base=(uintptr_t)window;
    tribes_set_last_error(123);uint32_t a=tribes_global_alloc(base,0,65544);assert(a==0x30000000&&tribes_get_last_error()==123);
    uint32_t b=tribes_global_alloc(base,0x40,9);assert(b>=a+65544);uint32_t span=tribes_global_size(base,a);assert(span>=65544);
    memset((void *)(uintptr_t)(base+a),0x79,span);assert(!tribes_global_free(base,a));
    uint32_t c=tribes_global_alloc(base,0x40,65544);assert(c==a);
    unsigned char *host=(void *)(uintptr_t)(base+c);for(uint32_t n=0;n<span;n++)assert(host[n]==0);
    assert(tribes_global_free(base,c+1)==c+1&&tribes_get_last_error()==6);
    assert(!tribes_global_free(base,c));assert(tribes_global_free(base,c)==c);
    assert(!tribes_global_free(base,b));assert(!tribes_global_free(base,0));
    assert(!tribes_global_alloc(base,0,UINT32_MAX)&&tribes_get_last_error()==8);
    uint32_t clean=tribes_global_alloc(base,0,65544);assert(clean==a);for(uint32_t i=0;i<tribes_global_size(base,clean);i++)assert(!*((unsigned char *)(uintptr_t)(base+clean)+i));memset((void *)(uintptr_t)(base+clean),0x79,tribes_global_size(base,clean));assert(!tribes_global_free(base,clean));
    clean=tribes_global_alloc(base,0,65544);assert(clean==a);for(uint32_t i=0;i<tribes_global_size(base,clean);i++)assert(!*((unsigned char *)(uintptr_t)(base+clean)+i));assert(!tribes_global_free(base,clean));
    uint32_t zero=tribes_global_alloc(base,0,0);assert(zero&&tribes_global_size(base,zero)>0);assert(!tribes_global_free(base,zero));
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    a=tribes_global_alloc(base,0,0x10000000);assert(a==0x30000000);assert(!tribes_global_free(base,a));
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: guest pointers, zero fill/reuse, free coalescing, invalid frees, overflow/OOM, 8000 concurrent lifetimes, last-error isolation");return 0;
}
