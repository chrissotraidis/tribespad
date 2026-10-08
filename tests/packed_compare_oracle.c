#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>
typedef struct {uint32_t op,control;unsigned char left[32],right[32];} Request;
typedef struct {uint32_t control;unsigned char value[32];} Response;
int main(void) {
    int translated=0;size_t size=sizeof translated;
    assert(!sysctlbyname("sysctl.proc_translated",&translated,&size,0,0)&&translated==1);
    Request q;Response r;
    while(fread(&q,sizeof q,1,stdin)==1) {
        uint32_t saved;
        __asm__ volatile("stmxcsr %0\n\tldmxcsr %1" : "=m"(saved) : "m"(q.control));
        if(q.op==0) {
            __asm__ volatile("vmovdqu %1, %%ymm0\n\tvmovdqu %2, %%ymm1\n\tcmpltps %%xmm1, %%xmm0\n\tvmovdqu %%ymm0, %0"
              : "=m"(r.value) : "m"(q.left),"m"(q.right) : "ymm0","ymm1","memory");
        } else {
            assert(q.op==1);
            __asm__ volatile("vmovdqu %1, %%ymm0\n\tvmovdqu %2, %%ymm1\n\tminps %%xmm1, %%xmm0\n\tvmovdqu %%ymm0, %0"
              : "=m"(r.value) : "m"(q.left),"m"(q.right) : "ymm0","ymm1","memory");
        }
        __asm__ volatile("stmxcsr %0\n\tldmxcsr %1\n\tvzeroupper" : "=m"(r.control) : "m"(saved) : "memory");
        assert(fwrite(&r,sizeof r,1,stdout)==1);
    }
    return ferror(stdin)?1:0;
}
