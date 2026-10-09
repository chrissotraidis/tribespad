#include "../port/core/sse_arithmetic.h"
#include "softfloat.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
typedef struct {unsigned op,control;uint64_t a,b,result;unsigned raised;} Case;
#include "cases.inc"
static void *worker(void *arg) {
    unsigned seed=(unsigned)(uintptr_t)arg;
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        const Case *test=&cases[i];
        softfloat_roundingMode=(seed+i)%4;
        softfloat_detectTininess=(seed+i)&1;
        softfloat_exceptionFlags=(seed+i)&31;
        extF80_roundingPrecision=(uint_fast8_t[]){32,64,80}[(seed+i)%3];
        unsigned round=softfloat_roundingMode,tiny=softfloat_detectTininess;
        unsigned flags=softfloat_exceptionFlags,precision=extF80_roundingPrecision;
        struct {uint64_t before,result,after;} output={0x1122334455667788,0xaaaaaaaaaaaaaaaa,0x8877665544332211};
        struct {uint32_t before,raised,after;} status={0x12345678,0xaaaaaaaa,0x87654321};
        assert(tb_sse_arithmetic(test->op,test->a,test->b,test->control,&output.result,&status.raised));
        if(output.result!=test->result || status.raised!=test->raised) {
            fprintf(stderr,"SSE arithmetic mismatch case %u op %u control %x inputs %llx/%llx result %llx/%llx flags %x/%x\n",i,test->op,test->control,(unsigned long long)test->a,(unsigned long long)test->b,(unsigned long long)output.result,(unsigned long long)test->result,status.raised,test->raised);assert(0);
        }
        TBSSEResult candidate=tb_sse_evaluate(test->op,test->a,test->b,test->control);
        assert(candidate.valid&&candidate.value==test->result&&candidate.raised==test->raised);
        assert(output.before==0x1122334455667788&&output.after==0x8877665544332211);
        assert(status.before==0x12345678&&status.after==0x87654321);
        assert(round==softfloat_roundingMode&&tiny==softfloat_detectTininess);
        assert(flags==softfloat_exceptionFlags&&precision==extF80_roundingPrecision);
    }
    uint64_t result=0xaaaaaaaaaaaaaaaa;uint32_t raised=0xaaaaaaaa;
    assert(!tb_sse_evaluate(6,0,0,0x1f80).valid);
    assert(!tb_sse_evaluate(0,0,0,0x10000).valid);
    assert(!tb_sse_arithmetic(6,0,0,0x1f80,&result,&raised));
    assert(!tb_sse_arithmetic(0,0,0,0x10000,&result,&raised));
    assert(!tb_sse_arithmetic(0,0,0,0x1f80,NULL,&raised));
    assert(!tb_sse_arithmetic(0,0,0,0x1f80,&result,NULL));
    assert(result==0xaaaaaaaaaaaaaaaa&&raised==0xaaaaaaaa);
    return NULL;
}
int main(void) {
    worker(NULL);pthread_t threads[8];
    for(unsigned n=0;n<8;n++)assert(!pthread_create(&threads[n],NULL,worker,(void *)(uintptr_t)(n+1)));
    for(unsigned n=0;n<8;n++)assert(!pthread_join(threads[n],NULL));
    printf("PASS: %zu scalar SSE arithmetic cases, guarded outputs, eight threads, complete SoftFloat TLS restoration\n",sizeof cases/sizeof cases[0]);return 0;
}
