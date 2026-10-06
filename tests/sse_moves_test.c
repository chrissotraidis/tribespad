#include "../port/llasm-support/llasm_cpu.h"
#include "../port/llasm-support/tribes_sse.h"
#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
typedef struct {
    unsigned kind,mode,dst,src,offset,value,flags,mxcsr,result;
    unsigned char initial[128],expected[128],memory[64],after[64];
} Case;
#include "cases.inc"
_Static_assert(offsetof(_cpu,_xmm)==176,"existing CPU prefix changed");
_Static_assert(offsetof(_cpu,_mxcsr)==304,"vector layout mismatch");
_Static_assert(offsetof(_cpu,_sse_reserved)==308,"old trailing padding moved");
_Static_assert(offsetof(_cpu,_ymm_hi)==312,"upper vector append layout mismatch");
_Static_assert(sizeof(_cpu)==440,"extended CPU size mismatch");
static void *worker(void *unused) {
    (void)unused;
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        const Case *test=&cases[i];unsigned char backing[97];
        /* Odd host bias tests guest alignment independently of native alignment. */
        unsigned char *data=backing+17;memset(backing,0x79,sizeof backing);memcpy(data,test->memory,64);
        _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)data-0x2000;cpu._eflags=test->flags;cpu._mxcsr=test->mxcsr;memcpy(cpu._xmm,test->initial,128);_cpu expected=cpu;memcpy(expected._xmm,test->expected,128);
        uint32_t result=0,address=0x2000+test->offset;
        switch(test->kind) {
        case 0:tb_sse_load(&cpu,test->dst,address,test->mode);break;
        case 1:tb_sse_store(&cpu,test->dst,address,test->mode);break;
        case 2:tb_sse_regmove(&cpu,test->dst,test->src,test->mode);break;
        case 3:tb_sse_setd(&cpu,test->dst,test->value);break;
        case 4:result=tb_sse_getd(&cpu,test->dst);break;
        default:assert(0);
        }
        assert(result==test->result&&!memcmp(&cpu,&expected,sizeof cpu));
        assert(!memcmp(data,test->after,64));for(unsigned n=0;n<17;n++)assert(backing[n]==0x79);for(unsigned n=81;n<sizeof backing;n++)assert(backing[n]==0x79);
    }
    return NULL;
}
static void reject(unsigned mode) {
    pid_t child=fork();assert(child>=0);
    if(!child) {
        unsigned char data[64];_cpu cpu={0};cpu._pointer_offset=(uintptr_t)data-0x2000;
        switch(mode) {
        case 0:tb_sse_load(&cpu,0,0x2001,TB_SSE_A128);break;
        case 1:tb_sse_store(&cpu,0,0x2001,TB_SSE_A128);break;
        case 2:tb_sse_load(&cpu,8,0x2000,TB_SSE_U128);break;
        case 3:tb_sse_load(&cpu,0,UINT32_MAX,TB_SSE_U128);break;
        case 4:tb_sse_regmove(&cpu,0,1,TB_SSE_LPD);break;
        case 5:tb_sse_load(&cpu,0,0x2000,99);break;
        }
        _exit(0);
    }
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);
}
int main(void) {
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);_cpu expected=cpu;memset(expected._xmm,0,128);memset(expected._ymm_hi,0,128);expected._mxcsr=0x1f80;tb_sse_init(&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu));
    for(unsigned n=0;n<6;n++)reject(n);
    worker(NULL);pthread_t threads[8];for(unsigned n=0;n<8;n++)assert(!pthread_create(&threads[n],NULL,worker,NULL));for(unsigned n=0;n<8;n++)assert(!pthread_join(threads[n],NULL));
    printf("PASS: %zu x86-oracle legacy SSE move cases, full CPU/MXCSR/x87/stack isolation, guest alignment rejection, guarded memory and eight independent native threads\n",sizeof cases/sizeof cases[0]);return 0;
}
