/* Reached legacy packed comparisons in the original float-to-byte converter.
   Reuse scalar SSE semantics, collecting exceptions before publishing lanes. */
#include "llasm_cpu.h"
#include "tribes_sse.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void packed(_cpu *cpu,unsigned dest,unsigned source,unsigned minimum) {
    if(!cpu||dest>=8||source>=8)abort();
    uint32_t left[4],right[4],value[4],raised=0,control=cpu->_mxcsr;
    memcpy(left,cpu->_xmm[dest],16);memcpy(right,cpu->_xmm[source],16);
    _cpu lane={0};
    for(unsigned i=0;i<4;++i) {
        lane._mxcsr=(control&~63u)|0x1f80u;
        memcpy(lane._xmm[0],&left[i],4);memcpy(lane._xmm[1],&right[i],4);
        if(minimum) {
            tb_sse_minmaxss(&lane,0,0,1);
            memcpy(&value[i],lane._xmm[0],4);
        } else {
            tb_avx_comiss(&lane,0,1);
            /* CF without PF is ordered less-than; unordered is false. */
            value[i]=(lane._eflags&5u)==1u?UINT32_MAX:0;
        }
        raised|=lane._mxcsr&63u;
    }
    cpu->_mxcsr|=raised;
    /* Retain the explicit unmasked-exception boundary, without partial XMM. */
    if(raised&~(control>>7))abort();
    memcpy(cpu->_xmm[dest],value,16);
}
void tb_sse_packed_cmpltps(_cpu *cpu,unsigned dest,unsigned source) {packed(cpu,dest,source,0);}
void tb_sse_packed_minps(_cpu *cpu,unsigned dest,unsigned source) {packed(cpu,dest,source,1);}
