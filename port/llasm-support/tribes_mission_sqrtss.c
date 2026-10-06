/* Exact legacy SQRTSS xmm1,xmm1 reached by the original mission method.
   Reuse the binary32 candidate/status adapter; preserve all other CPU state. */
#include "llasm_cpu.h"
#include "../core/sse_arithmetic.h"
#include <stdlib.h>
#include <string.h>
void tb_mission_sqrtss(_cpu *cpu){
 uint32_t input,raised,control=cpu->_mxcsr;uint64_t result;
 memcpy(&input,cpu->_xmm[1],4);
 if(!tb_sse_sqrt32(input,control,&result,&raised))abort();
 cpu->_mxcsr|=raised;
 /* Retain the destination on an unmasked exception. Precise Windows SIMD
    exception delivery remains a separate, unfinished runtime contract. */
 if(raised&~(control>>7))abort();
 uint32_t low=(uint32_t)result;memcpy(cpu->_xmm[1],&low,4);
}
