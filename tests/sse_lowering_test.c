#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "softfloat.h"
typedef struct {
    unsigned index,address,size,mxcsr,expected_mxcsr;
    uint32_t general[9],expected_general[9];
    unsigned char initial[128],expected[128],memory[16],after[16];
} Case;
#include "cases.inc"
static void *worker(void *unused) {
    (void)unused;
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        const Case *test=&cases[i];unsigned char memory[48];memset(memory,0x79,48);memcpy(memory+16,test->memory,16);
        _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)(memory+16)-test->address;cpu._mxcsr=test->mxcsr;memcpy(&cpu._eax,test->general,36);memcpy(cpu._xmm,test->initial,128);_cpu expected=cpu;expected._mxcsr=test->expected_mxcsr;memcpy(&expected._eax,test->expected_general,36);memcpy(expected._xmm,test->expected,128);
        softfloat_roundingMode=i%4;softfloat_detectTininess=i&1;
        softfloat_exceptionFlags=i&31;extF80_roundingPrecision=(uint_fast8_t[]){32,64,80}[i%3];
        unsigned round=softfloat_roundingMode,tiny=softfloat_detectTininess;
        unsigned flags=softfloat_exceptionFlags,precision=extF80_roundingPrecision;
        functions[test->index](&cpu);
        assert(round==softfloat_roundingMode&&tiny==softfloat_detectTininess);
        assert(flags==softfloat_exceptionFlags&&precision==extF80_roundingPrecision);
        if(memcmp(&cpu,&expected,sizeof cpu)||memcmp(memory+16,test->after,16)) {
            fprintf(stderr,"SSE lowering mismatch: case %u block %u\n",i,test->index);assert(0);
        }
        for(unsigned n=0;n<16;n++)assert(memory[n]==0x79&&memory[32+n]==0x79);
    }
    return NULL;
}
int main(void) {
    worker(NULL);pthread_t threads[8];for(unsigned n=0;n<8;n++)assert(!pthread_create(&threads[n],NULL,worker,NULL));for(unsigned n=0;n<8;n++)assert(!pthread_join(threads[n],NULL));
    printf("PASS: %zu x86-oracle cases execute actual AOT legacy SIMD bodies; full CPU/vector/MXCSR/x87/stack and guarded-memory comparison, eight independent native threads\n",sizeof cases/sizeof cases[0]);return 0;
}
