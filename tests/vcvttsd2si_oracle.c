/* Canonical W0 REG/MEM x86 reference; not linked into the client. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RUN(instruction) __asm__ volatile( \
 "ldmxcsr %[control]\n\tvmovdqu %[input], %%ymm1\n\t" \
 "pushq %[flags]\n\tpopfq\n\t" instruction "\n\t" \
 "movl %%eax, %[result]\n\tpushfq\n\tpopq %[after_flags]\n\t" \
 "stmxcsr %[after_control]\n\tvmovdqu %%ymm1, %[output]\n\t" \
 "ldmxcsr %[saved]\n\tvzeroupper" \
 : [result] "=&r"(result),[after_flags] "=&r"(after_flags),[after_control] "=m"(after_control),[output] "=m"(output),[memory] "+m"(*memory) \
 : [control] "m"(control),[input] "m"(input),[flags] "r"(flags),[saved] "m"(saved) \
 : "eax","ymm1","cc","memory")
int main(void) {
    unsigned mode,control,initial_flags;unsigned long long source;
    while(scanf("%u %llx %x %x",&mode,&source,&control,&initial_flags)==4) {
        if(mode>1||control>0xffff||(control&0x1f80)!=0x1f80)return 2;
        uint64_t input[4]={source,UINT64_C(0x123456789abcdef0),UINT64_C(0xfedcba9876543210),UINT64_C(0x55555555aaaaaaaa)},output[4],flags,after_flags;
        uint32_t saved,after_control,result;
        unsigned char backing[17];memset(backing,0xa5,sizeof backing);unsigned char (*memory)[8]=(void *)(backing+1);memcpy(*memory,&source,8);
        __asm__ volatile("stmxcsr %0; pushfq; popq %1":"=m"(saved),"=r"(flags));flags=(flags&~UINT64_C(0x8d5))|(initial_flags&0x8d5);
        if(mode) {RUN("vcvttsd2si %[memory], %%eax");} else {RUN("vcvttsd2si %%xmm1, %%eax");}
        if(memcmp(input,output,sizeof input)||memcmp(*memory,&source,8))return 3;
        for(unsigned n=0;n<sizeof backing;n++)if((n<1||n>=9)&&backing[n]!=0xa5)return 3;
        printf("%08x %04x %03llx %016llx %016llx %016llx %016llx\n",result,after_control,(unsigned long long)(after_flags&0x8d5),(unsigned long long)output[0],(unsigned long long)output[1],(unsigned long long)output[2],(unsigned long long)output[3]);
    }
    return ferror(stdin)?1:0;
}
