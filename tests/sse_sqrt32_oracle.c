/* Offline x86 reference; repeated packed lanes isolate one unary result. */
#include <stdint.h>
#include <stdio.h>
int main(void) {
    uint32_t source,control;
    while(scanf("%x %x",&source,&control)==2) {
        if(control>0xffff||(control&0x1f80)!=0x1f80)return 2;
        uint32_t input[8],output[8],saved,after;
        for(unsigned n=0;n<8;n++)input[n]=source;
        __asm__ volatile("stmxcsr %[saved]\n\tldmxcsr %[control]\n\t"
            "vmovdqu %[input], %%ymm1\n\tvsqrtps %%ymm1, %%ymm0\n\t"
            "vmovdqu %%ymm0, %[output]\n\tstmxcsr %[after]\n\t"
            "ldmxcsr %[saved]\n\tvzeroupper"
            : [saved] "=m"(saved), [output] "=m"(output), [after] "=m"(after)
            : [control] "m"(control), [input] "m"(input)
            : "ymm0","ymm1","memory");
        for(unsigned n=1;n<8;n++)if(output[n]!=output[0])return 3;
        printf("%08x %04x\n",output[0],after);
    }
    return ferror(stdin)?1:0;
}
