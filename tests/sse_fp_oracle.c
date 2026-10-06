/* Offline x86-64 SSE oracle, run through Rosetta on the build Mac.
   Never linked into the Apple client. All exceptions are masked here. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RUN(instruction) __asm__ volatile( \
    "ldmxcsr %[control]\n\t" \
    "movdqu %[a], %%xmm0\n\t" \
    "movdqu %[b], %%xmm1\n\t" \
    "pushq %[flags]\n\tpopfq\n\t" \
    "movl $0, %%eax\n\t" instruction "\n\t" \
    "pushfq\n\tpopq %[after_flags]\n\t" \
    "stmxcsr %[after_control]\n\t" \
    "movdqu %%xmm0, %[result]\n\t" \
    "ldmxcsr %[saved]\n\t" \
    : [after_flags] "=&r"(after_flags), [after_control] "=m"(after_control), \
      [result] "=m"(result), "=a"(integer) \
    : [control] "m"(control), [a] "m"(a), [b] "m"(b), \
      [flags] "r"(flags), [saved] "m"(saved) \
    : "xmm0", "xmm1", "cc", "memory")
int main(void) {
    unsigned op, control, initial_flags;
    unsigned long long a0,a1,b0,b1;
    while(scanf("%u %x %x %llx %llx %llx %llx", &op,&control,&initial_flags,&a0,&a1,&b0,&b1)==7) {
        if(op>5 || (control & 0x1f80)!=0x1f80 || control>0xffff) return 2;
        uint64_t a[2]={a0,a1}, b[2]={b0,b1}, result[2], flags, after_flags;
        uint32_t saved, after_control, integer;
        __asm__ volatile("stmxcsr %0; pushfq; popq %1":"=m"(saved),"=r"(flags));
        flags=(flags & ~UINT64_C(0x8d5)) | (initial_flags & 0x8d5);
        switch(op) {
        case 0: RUN("addsd %%xmm1, %%xmm0"); break;
        case 1: RUN("subsd %%xmm1, %%xmm0"); break;
        case 2: RUN("mulss %%xmm1, %%xmm0"); break;
        case 3: RUN("cvtpd2ps %%xmm1, %%xmm0"); break;
        case 4: RUN("cvttss2si %%xmm1, %%eax"); break;
        default: RUN("ucomisd %%xmm1, %%xmm0"); break;
        }
        printf("%016llx %016llx %08x %08x %03llx\n",
            (unsigned long long)result[0],(unsigned long long)result[1],integer,
            after_control,(unsigned long long)(after_flags & 0x8d5));
    }
    return ferror(stdin)?1:0;
}
