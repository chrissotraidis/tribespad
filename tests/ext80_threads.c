#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "ext80.h"
#define LITTLEENDIAN 1
#include "softfloat.h"

static void *worker(void *arg) {
    uintptr_t n=(uintptr_t)arg;
    softfloat_roundingMode=softfloat_round_odd;
    softfloat_exceptionFlags=softfloat_flag_invalid;
    extF80_roundingPrecision=64;
    softfloat_detectTininess=softfloat_tininess_beforeRounding;
    /* 1 + 2^-63 rounds differently under every precision/rounding choice. */
    TBExt80 a={UINT64_C(0x8000000000000000),0x3fff};
    TBExt80 b={UINT64_C(0x8000000000000000),0x3fc0},z;
    uint16_t f,cw=0x7f|((n%2?3:0)<<8)|((n%4)<<10);
    uint64_t expected=n%2 ? UINT64_C(0x8000000000000001)
        : (n%4==2 ? UINT64_C(0x8000010000000000) : UINT64_C(0x8000000000000000));
    for (unsigned i=0;i<5000;i++) {
        if(!tb_ext80_arithmetic(TB_EXT_ADD,a,b,cw,&z,&f) || z.significand!=expected || z.sign_exponent!=0x3fff || f!=(n%2?0:32)) return (void *)1;
        if(softfloat_roundingMode!=softfloat_round_odd || softfloat_exceptionFlags!=softfloat_flag_invalid || extF80_roundingPrecision!=64 || softfloat_detectTininess!=softfloat_tininess_beforeRounding) return (void *)2;
    }
    return 0;
}
int main(void) {
    pthread_t threads[8];
    for(uintptr_t i=0;i<8;i++) if(pthread_create(&threads[i],0,worker,(void *)i)) return 3;
    for(unsigned i=0;i<8;i++) {void *result;if(pthread_join(threads[i],&result)||result) return 4;}
    puts("PASS: 40000 parallel extended80 operations; TLS configuration restored");
    return 0;
}
