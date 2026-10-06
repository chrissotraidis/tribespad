#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>
typedef struct { uint32_t control; unsigned char vector[32]; } Request;
int main(void) {
    int translated=0; size_t size=sizeof translated;
    assert(!sysctlbyname("sysctl.proc_translated",&translated,&size,0,0)&&translated==1);
    Request request, response;
    while (fread(&request,sizeof request,1,stdin)==1) {
        uint32_t saved;
        __asm__ volatile("stmxcsr %[saved]\n\tldmxcsr %[control]\n\t"
                         "vmovdqu %[input], %%ymm1\n\t"
                         ".byte 0xf3,0x0f,0x51,0xc9\n\t"
                         "vmovdqu %%ymm1, %[output]\n\t"
                         "stmxcsr %[status]\n\tldmxcsr %[saved]\n\tvzeroupper"
                         : [saved]"=m"(saved),[output]"=m"(response.vector),[status]"=m"(response.control)
                         : [control]"m"(request.control),[input]"m"(request.vector)
                         : "ymm1","memory");
        assert(fwrite(&response,sizeof response,1,stdout)==1);
    }
    return ferror(stdin)?1:0;
}
