#include "guest_processor_features.h"
#include "guest_cpu.h"
#include <stdio.h>
#include <stdlib.h>
/* Windows PF_* values describe the guest, not the Apple host. Keep the
 * advertised capabilities consistent with x86_cpuid and its fixed profile.
 * https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent */
uint32_t tribes_processor_feature_present(uint32_t feature) {
    _cpu cpu={0};cpu._eax=1;x86_cpuid(&cpu);
    switch(feature) {
    case 3:return (cpu._edx>>23)&1; /* MMX */
    case 6:return (cpu._edx>>25)&1; /* SSE */
    case 8:return (cpu._edx>>4)&1;  /* RDTSC */
    case 10:return (cpu._edx>>26)&1;/* SSE2 */
    case 13:return cpu._ecx&1;     /* SSE3 */
    case 23:return 0;              /* PF_FASTFAIL_AVAILABLE: no __fastfail (int 0x29) in the guest */
    default:fprintf(stderr,"TribesPad: unsupported Windows processor feature %u\n",feature);abort();
    }
}
uint32_t tribes_sdl_has_mmx(void){return tribes_processor_feature_present(3);}
uint32_t tribes_sdl_has_sse(void){return tribes_processor_feature_present(6);}
uint32_t tribes_sdl_has_avx(void){
    _cpu cpu={0};cpu._eax=1;x86_cpuid(&cpu);
    if(!(cpu._ecx&0x10000000u))return 0;
    /* SDL also requires OS YMM save support. The current guest profile has
       no AVX/XSAVE/XGETBV contract; a future profile must establish it. */
    fputs("TribesPad: guest AVX OS context contract not implemented\n",stderr);abort();
}
