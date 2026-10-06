/* Millisecond request granularity over the native monotonic/nanosleep backend.
 * This backend always uses its finest clock/deadline representation. A period
 * request never changes global Darwin scheduling or promises wake-up latency.
 * The 65535 ms ceiling is this runtime's declared multimedia timer profile.
 * Period references belong to the single guest process hosted by this runtime.
 * Callback/event timers remain separate, unimplemented services.
 * Contracts and limitations: config/runtime/multimedia-timer.json. */
#include "multimedia_timer.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define PERIOD_MAX 65535u
#define TIMERR_NOCANDO 97u
static pthread_mutex_t period_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t references[PERIOD_MAX+1];
static uint32_t minimum_period(void){
    struct timespec resolution;
    if(clock_getres(CLOCK_MONOTONIC,&resolution)||resolution.tv_sec<0||
       resolution.tv_sec>65||resolution.tv_nsec<0||resolution.tv_nsec>=1000000000)return 0;
    uint64_t ns=(uint64_t)resolution.tv_sec*1000000000+(uint64_t)resolution.tv_nsec;
    if(!ns)return 0;
    uint64_t ms=(ns+999999)/1000000;
    return ms<=PERIOD_MAX?(uint32_t)ms:0;
}
uint32_t tribes_time_caps(uint64_t base,uint32_t output,uint32_t bytes){
    if(!base||!output||bytes<8||(uint64_t)output+8>UINT64_C(0x100000000)||
       base>UINTPTR_MAX-output-7)return TIMERR_NOCANDO;
    uint32_t caps[2]={minimum_period(),PERIOD_MAX};
    if(!caps[0])return TIMERR_NOCANDO;
    memcpy((void *)(uintptr_t)(base+output),caps,sizeof caps);
    return 0;
}
static uint32_t period_request(uint32_t period,int begin){
    uint32_t minimum=minimum_period();
    if(!minimum||period<minimum||period>PERIOD_MAX)return TIMERR_NOCANDO;
    if(pthread_mutex_lock(&period_lock))abort();
    uint32_t result=0;
    if(begin){if(references[period]==UINT32_MAX)result=TIMERR_NOCANDO;else ++references[period];}
    else {if(!references[period])result=TIMERR_NOCANDO;else --references[period];}
    if(pthread_mutex_unlock(&period_lock))abort();
    return result;
}
uint32_t tribes_time_begin_period(uint32_t period){return period_request(period,1);}
uint32_t tribes_time_end_period(uint32_t period){return period_request(period,0);}
