#include "../port/core/performance_clock.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static uint64_t now(void) { struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec; }
static uint64_t wall(void) { struct timespec t;assert(!clock_gettime(CLOCK_REALTIME,&t));return ((uint64_t)t.tv_sec+11644473600)*10000000+(uint64_t)t.tv_nsec/100; }
int main(void) {
    unsigned char result[16];memset(result,0xa5,sizeof result);uint32_t address=17;uint64_t base=(uintptr_t)(result+1)-address;
    tribes_set_last_error(123);assert(tribes_performance_frequency(base,address));uint64_t frequency;memcpy(&frequency,result+1,8);assert(frequency==1000000000&&tribes_get_last_error()==123);
    uint64_t previous=0;
    for(unsigned i=0;i<10000;i++) {
        uint64_t before=now();assert(tribes_performance_counter(base,address));uint64_t after=now(),counter;memcpy(&counter,result+1,8);
        assert(before<=counter&&counter<=after&&counter>=previous);previous=counter;
        before=now()/1000000;uint32_t tick=tribes_tick_count();after=now()/1000000;
        assert((uint32_t)(tick-(uint32_t)before)<=(uint32_t)(after-before));
        assert(tribes_get_last_error()==123);
        before=wall();tribes_system_file_time(base,address);after=wall();memcpy(&counter,result+1,8);
        assert(before<=counter&&counter<=after&&tribes_get_last_error()==123);
    }
    assert(result[0]==0xa5);for(unsigned i=9;i<16;i++)assert(result[i]==0xa5);
    assert(!tribes_performance_frequency(base,0)&&tribes_get_last_error()==87);
    assert(!tribes_performance_counter(base,UINT32_MAX));
    puts("PASS: 10000 native monotonic, boot-millisecond and UTC FILETIME samples, stable frequency, unaligned guest int64 outputs and guards");return 0;
}
