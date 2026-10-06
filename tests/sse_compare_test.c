#include "../port/llasm-support/llasm_cpu.h"
#include "../port/llasm-support/tribes_sse.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint64_t a,a1,b,b1;uint32_t mxcsr,flags,after_mxcsr,after_flags;} Case;
#include "cases.inc"
static void *worker(void *unused) {
    (void)unused;
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        const Case *test=&cases[i];
        for(unsigned mode=0;mode<4;mode++) {
            if(mode==3 && test->a!=test->b)continue;
            unsigned left=mode?7:0,right=mode==3?7:mode?5:1;
            unsigned char memory[32];memset(memory,0x79,sizeof memory);
            memcpy(memory+3,&test->b,8);
            _cpu cpu;memset(&cpu,0xa5,sizeof cpu);
            cpu._pointer_offset=(uintptr_t)(memory+3)-0x2003;
            memcpy(cpu._xmm[left],&test->a,8);memcpy(cpu._xmm[left]+8,&test->a1,8);
            memcpy(cpu._xmm[right],&test->b,8);memcpy(cpu._xmm[right]+8,&test->b1,8);
            cpu._mxcsr=test->mxcsr;cpu._eflags=0x202|test->flags;
            _cpu expected=cpu;expected._mxcsr=test->after_mxcsr;
            expected._eflags=(expected._eflags&~0x8d5)|test->after_flags;
            if(mode==2)tb_sse_ucomisdmem(&cpu,left,0x2003);
            else tb_sse_ucomisd(&cpu,left,right);
            if(memcmp(&cpu,&expected,sizeof cpu)) {
                fprintf(stderr,"compare mismatch: case %u mode %u MXCSR %x/%x flags %x/%x\n",i,mode,cpu._mxcsr,expected._mxcsr,cpu._eflags,expected._eflags);assert(0);
            }
            unsigned char guard[32];memset(guard,0x79,sizeof guard);memcpy(guard+3,&test->b,8);
            assert(!memcmp(memory,guard,sizeof guard));
        }
    }
    return NULL;
}
int main(void) {
    worker(NULL);pthread_t threads[8];
    for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    printf("PASS: %zu masked UCOMISD references, three register/memory forms plus equal-bit self-aliases, full CPU and memory guards, eight independent native threads\n",sizeof cases/sizeof cases[0]);return 0;
}
