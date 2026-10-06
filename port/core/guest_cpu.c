#include "guest_cpu.h"
/* Intel family 6/model 3/stepping 3 is a stable compatibility identity.
   Only the native extended80 and MMX implementations are advertised.
   TSC, CX8, CMOV, SSE and 3DNow capability bits remain clear. CPUID does
   not change EFLAGS, stack, unrelated registers, or floating-point state.
   Out-of-range queries return the highest basic leaf (Intel contract). */
void x86_cpuid(_cpu *cpu) {
    uint32_t leaf=cpu->_eax;
    uint32_t a=0x633,b=0,c=0,d=0x00800001;
    switch(leaf) {
    case 0: a=1;b=0x756e6547;d=0x49656e69;c=0x6c65746e;break;
    case 0x80000000: a=0x80000001;d=0;break;
    case 0x80000001: a=0;d=0;break;
    default: break;
    }
    cpu->_eax=a;cpu->_ebx=b;cpu->_ecx=c;cpu->_edx=d;
}
