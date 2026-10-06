/* QueryPerformance* clock with a stable nanosecond tick frequency.
   Counter samples use the native monotonic clock, independent of wall-time changes.
   Contract: https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter */
#include "performance_clock.h"
#include "guest_heap.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
static uint32_t store(uint64_t base,uint32_t output,uint64_t value) {
    if(!output||output>UINT32_MAX-7||base>UINTPTR_MAX-output-7) { tribes_set_last_error(87);return 0; }
    memcpy((void *)(uintptr_t)(base+output),&value,8);
#ifdef TRIBESPAD_CLOCK_OBSERVER
    extern void tribes_clock_observation(uint32_t,uint64_t);
    tribes_clock_observation(output,value);
#endif
    return 1;
}
uint32_t tribes_performance_frequency(uint64_t base,uint32_t output) {
    return store(base,output,UINT64_C(1000000000));
}
uint32_t tribes_performance_counter(uint64_t base,uint32_t output) {
    struct timespec sample;
    if(clock_gettime(CLOCK_MONOTONIC,&sample)!=0||sample.tv_sec<0||(uint64_t)sample.tv_sec>INT64_MAX/UINT64_C(1000000000)) { tribes_set_last_error(31);return 0; }
    uint64_t value=(uint64_t)sample.tv_sec*UINT64_C(1000000000)+(uint64_t)sample.tv_nsec;
    return store(base,output,value);
}

/* https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount
   Milliseconds in the native boot clock domain, modulo 2^32. No per-process epoch. */
uint32_t tribes_tick_count(void) {
    struct timespec sample;
    if(clock_gettime(CLOCK_MONOTONIC,&sample)!=0||sample.tv_sec<0)abort();
    uint32_t value=(uint32_t)((uint64_t)sample.tv_sec*1000+(uint64_t)sample.tv_nsec/1000000);
#ifdef TRIBESPAD_CLOCK_OBSERVER
    extern void tribes_clock_observation(uint32_t,uint64_t);
    tribes_clock_observation(0,value);
#endif
    return value;
}

/* FILETIME: UTC wall time in 100 ns units since 1601-01-01.
   https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtimeasfiletime */
void tribes_system_file_time(uint64_t base,uint32_t output) {
    struct timespec sample;
    if(clock_gettime(CLOCK_REALTIME,&sample)!=0||sample.tv_sec < -INT64_C(11644473600))abort();
    uint64_t seconds=(uint64_t)sample.tv_sec+UINT64_C(11644473600);
    uint64_t fraction=(uint64_t)sample.tv_nsec/100;
    if(seconds>(UINT64_MAX-fraction)/UINT64_C(10000000))abort();
    uint64_t value=seconds*UINT64_C(10000000)+fraction;
    if(!store(base,output,value))abort();
}

/* Finite non-alertable delay; zero yields to the native scheduler. Windows
   tick/priority policy is not emulated. Infinite guest thread waits are a gate. */
void tribes_sleep(uint32_t milliseconds) {
    if(milliseconds==UINT32_MAX)abort();
    if(!milliseconds) { if(sched_yield())abort();return; }
    struct timespec remaining={milliseconds/1000,(long)(milliseconds%1000)*1000000};
    while(nanosleep(&remaining,&remaining)) {
        if(errno!=EINTR)abort();
#ifdef TRIBESPAD_SLEEP_OBSERVER
        extern void tribes_sleep_interruption(void);
        tribes_sleep_interruption();
#endif
    }
}
