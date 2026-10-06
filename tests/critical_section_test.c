#include "../port/core/critical_section.h"
#include "../port/core/guest_heap.h"
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint8_t storage[32];
static uint64_t base;
static const uint32_t guest=0x25034;
static uint64_t shared;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static int attempted;
static void *contender(void *unused) {
    (void)unused;
    assert(!tribes_critical_try_enter(base,guest));
    pthread_mutex_lock(&gate);attempted=1;pthread_cond_signal(&condition);pthread_mutex_unlock(&gate);
    tribes_critical_enter(base,guest);shared=7;tribes_critical_leave(base,guest);return NULL;
}
static void *increment(void *unused) {
    (void)unused;
    for(unsigned i=0;i<20000;i++) {
        tribes_critical_enter(base,guest);
        assert(tribes_critical_try_enter(base,guest));
        uint64_t previous=shared;shared=previous+1;
        tribes_critical_leave(base,guest);tribes_critical_leave(base,guest);
    }
    return NULL;
}
int main(void) {
    base=(uintptr_t)storage-guest;memset(storage,0xa5,sizeof storage);tribes_set_last_error(123);assert(tribes_critical_init_spin(base,guest,4000));assert(tribes_get_last_error()==123);
    uint32_t spin;memcpy(&spin,storage+20,4);assert(spin==(sysconf(_SC_NPROCESSORS_ONLN)>1?4000u:0u));
    for(unsigned i=24;i<32;i++)assert(storage[i]==0xa5);
    tribes_critical_enter(base,guest);assert(tribes_critical_try_enter(base,guest));
    pthread_t t;assert(!pthread_create(&t,NULL,contender,NULL));
    pthread_mutex_lock(&gate);while(!attempted)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);
    assert(shared==0);tribes_critical_leave(base,guest);assert(shared==0);
    tribes_critical_leave(base,guest);assert(!pthread_join(t,NULL));assert(shared==7);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,increment,NULL));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(shared==160007);
    tribes_critical_delete(base,guest);
    /* Reuse both a guest address and independent image windows without registry tombstones. */
    for(unsigned i=0;i<3000;i++) { tribes_critical_init(base,guest);tribes_critical_enter(base,guest);tribes_critical_leave(base,guest);tribes_critical_delete(base,guest); }
    uint8_t other[24];uint64_t base2=(uintptr_t)other-guest;
    tribes_critical_init(base,guest);tribes_critical_init(base2,guest);
    tribes_critical_enter(base,guest);assert(tribes_critical_try_enter(base2,guest));
    tribes_critical_leave(base2,guest);tribes_critical_leave(base,guest);
    tribes_critical_delete(base2,guest);tribes_critical_delete(base,guest);
    tribes_set_last_error(123);assert(tribes_critical_init_ex(base,guest,0,0));assert(tribes_get_last_error()==123);
    tribes_critical_enter(base,guest);assert(tribes_critical_try_enter(base,guest));
    tribes_critical_leave(base,guest);tribes_critical_leave(base,guest);tribes_critical_delete(base,guest);
    assert(tribes_critical_init_ex(base2,guest,4000,0));tribes_critical_enter(base2,guest);tribes_critical_leave(base2,guest);tribes_critical_delete(base2,guest);
    puts("PASS: recursive ownership, contended try/wait, 160000 protected updates, 3000 lifetimes, independent guest images, real 4000-count spin initialization/recursion/contention");return 0;
}
