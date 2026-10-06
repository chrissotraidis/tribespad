#include "../port/llasm-support/llasm_cpu.h"
#include "../port/llasm-support/tribes_sse.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
typedef struct {
    unsigned kind,dest,merge,source,offset,flags,mxcsr;
    unsigned char initial[256],expected[256],memory[64],after[64];
} Case;
#include "cases.inc"
static void *worker(void *unused) {
    (void)unused;
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        const Case *test=&cases[i];unsigned char backing[97];
        memset(backing,0x79,sizeof backing);unsigned char *data=backing+17;
        memcpy(data,test->memory,64);
        _cpu cpu;memset(&cpu,0xa5,sizeof cpu);
        cpu._pointer_offset=(uintptr_t)data-0x2000;
        cpu._eflags=test->flags;cpu._mxcsr=test->mxcsr;
        memcpy(cpu._xmm,test->initial,128);memcpy(cpu._ymm_hi,test->initial+128,128);
        _cpu expected=cpu;memcpy(expected._xmm,test->expected,128);
        memcpy(expected._ymm_hi,test->expected+128,128);
        switch(test->kind) {
        case 0:case 1:tb_avx_ssreg(&cpu,test->dest,test->merge,test->source);break;
        case 2:tb_avx_ssload(&cpu,test->dest,0x2000+test->offset);break;
        case 3:tb_avx_ssstore(&cpu,test->source,0x2000+test->offset);break;
        default:assert(0);
        }
        assert(!memcmp(&cpu,&expected,sizeof cpu));
        assert(!memcmp(data,test->after,64));
        for(unsigned n=0;n<17;n++)assert(backing[n]==0x79);
        for(unsigned n=81;n<sizeof backing;n++)assert(backing[n]==0x79);
    }
    return NULL;
}
static void reject(unsigned mode) {
    pid_t child=fork();assert(child>=0);
    if(!child) {
        unsigned char data[64];_cpu cpu={0};cpu._pointer_offset=(uintptr_t)data-0x2000;
        switch(mode) {
        case 0:tb_avx_ssreg(&cpu,8,0,0);break;
        case 1:tb_avx_ssreg(&cpu,0,8,0);break;
        case 2:tb_avx_ssreg(&cpu,0,0,8);break;
        case 3:tb_avx_ssload(&cpu,0,UINT32_MAX-2);break;
        case 4:tb_avx_ssstore(&cpu,0,UINT32_MAX-2);break;
        case 5:tb_avx_ssload(NULL,0,0x2000);break;
        case 6:cpu._pointer_offset=UINTPTR_MAX;tb_avx_ssstore(&cpu,0,0x2000);break;
        }
        _exit(0);
    }
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);
}
int main(void) {
    for(unsigned n=0;n<7;n++)reject(n);
    worker(NULL);pthread_t threads[8];
    for(unsigned n=0;n<8;n++)assert(!pthread_create(&threads[n],NULL,worker,NULL));
    for(unsigned n=0;n<8;n++)assert(!pthread_join(threads[n],NULL));
    printf("PASS: %zu Rosetta-reference VMOVSS helper cases; all register role aliases, full CPU/YMM/MXCSR/flags preservation, guarded unaligned memory and eight native threads\n",sizeof cases/sizeof cases[0]);
    return 0;
}
