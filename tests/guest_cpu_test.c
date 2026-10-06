#include "../port/core/guest_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static void *exercise(void *unused) {
    (void)unused;
    const uint32_t leaves[]={0,1,0x80000000,0x80000001,2,0x7fffffff,0x80000002,0xffffffff};
    const uint32_t results[][4]={
        {1,0x6c65746e,0x49656e69,0x756e6547},
        {0x633,0,0x00800001,0},
        {0x80000001,0,0,0},{0,0,0,0},
        {0x633,0,0x00800001,0},{0x633,0,0x00800001,0},
        {0x633,0,0x00800001,0},{0x633,0,0x00800001,0}};
    for(unsigned n=0;n<16000;n++) {
        _cpu state,before;memset(&state,0xa5,sizeof state);
        state._eax=leaves[n%8];state._ecx=n;state._eflags=(n*0x10201)^0x202;
        memcpy(&before,&state,sizeof state);x86_cpuid(&state);
        assert(!memcmp(&state,results[n%8],16));
        assert(!memcmp((char *)&state+16,(char *)&before+16,sizeof state-16));
    }
    return NULL;
}
int main(void) {
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,exercise,NULL));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: 128000 guest CPUID queries preserve flags, stack, FP state and unrelated registers under contention");
}
