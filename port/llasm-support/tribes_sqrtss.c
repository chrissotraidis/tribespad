/* Scalar legacy SQRTSS used by the newly discovered QoL detour. */
#include "llasm_cpu.h"
#include "../core/sse_arithmetic.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
void tb_sse_sqrtss(_cpu *cpu,unsigned dest,unsigned source){
 if(!cpu||dest>=8||source>=8)abort();
 uint32_t input,status,control=cpu->_mxcsr;uint64_t result;
 memcpy(&input,cpu->_xmm[source],4);
 if(!tb_sse_sqrt32(input,control,&result,&status))abort();
 cpu->_mxcsr|=status;
 /* Preserve the existing explicit boundary for unmasked SIMD faults. */
 if(status&~(control>>7))abort();
 uint32_t value=(uint32_t)result;memcpy(cpu->_xmm[dest],&value,4);
}
