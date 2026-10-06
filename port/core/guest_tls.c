/* Native thread-local Win32 TLS values, stored as original guest DWORDs.
   A generation change clears reused slots in every thread without scanning them.
   Contracts: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsalloc
   https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue */
#include "guest_tls.h"
#include "guest_heap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define SLOT_COUNT 1088
static struct { uint64_t generation;int used; } slots[SLOT_COUNT];
static _Thread_local struct { uint64_t generation;uint32_t value; } local[SLOT_COUNT];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
uint32_t tribes_tls_alloc(void) {
    pthread_mutex_lock(&lock);
    for(uint32_t i=0;i<SLOT_COUNT;i++)if(!slots[i].used) {
        if(slots[i].generation==UINT64_MAX){fprintf(stderr,"TribesPad: TLS generation exhausted\n");abort();}
        ++slots[i].generation;slots[i].used=1;pthread_mutex_unlock(&lock);return i;
    }
    pthread_mutex_unlock(&lock);tribes_set_last_error(8);return UINT32_MAX;
}
uint32_t tribes_tls_free(uint32_t index) {
    pthread_mutex_lock(&lock);
    if(index>=SLOT_COUNT||!slots[index].used){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
    slots[index].used=0;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_tls_get(uint32_t index) {
    pthread_mutex_lock(&lock);
    if(index>=SLOT_COUNT||!slots[index].used){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
    uint32_t value=local[index].generation==slots[index].generation?local[index].value:0;
    pthread_mutex_unlock(&lock);tribes_set_last_error(0);return value;
}
uint32_t tribes_tls_set(uint32_t index,uint32_t value) {
    pthread_mutex_lock(&lock);
    if(index>=SLOT_COUNT||!slots[index].used){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
    local[index].value=value;local[index].generation=slots[index].generation;
    pthread_mutex_unlock(&lock);return 1;
}
