#include "llasm_cpu.h"
#include "../core/quadword_conversion.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static void publish(CPU,unsigned op,unsigned dest,const unsigned char source[16]) {
    if(!cpu || op>TB_QC_I64_PS || dest>=8)abort();
    unsigned char output[16]={0};unsigned step=op<=TB_QC_PS_I64?4:8;
    uint32_t control=cpu->_mxcsr,raised=0;
    for(unsigned lane=0;lane<2;++lane) {
        uint64_t input=0,result;uint32_t flags;
        memcpy(&input,source+lane*step,step);
        if(!tb_quadword_conversion(op,input,control,&result,&flags))abort();
        raised|=flags;memcpy(output+lane*(op==TB_QC_I64_PS?4:8),&result,op==TB_QC_I64_PS?4:8);
    }
    cpu->_mxcsr|=raised;
    /* No destination publication on an unserviced SIMD exception. Windows
       fault delivery and precise unmasked packed priority remain unfinished. */
    if(raised&~(control>>7))abort();
    memcpy(cpu->_xmm[dest],output,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_evex_quadconv128(CPU,unsigned op,unsigned dest,unsigned source) {
    if(!cpu || op>TB_QC_I64_PS || dest>=8 || source>=8)abort();
    unsigned char input[16];memcpy(input,cpu->_xmm[source],16);publish(cpu,op,dest,input);
}
void tb_evex_quadconv128mem(CPU,unsigned op,unsigned dest,uint32_t address) {
    if(!cpu || op>TB_QC_I64_PS || dest>=8)abort();
    unsigned size=op<=TB_QC_PS_I64?8:16;uint64_t end=(uint64_t)address+size;
    uintptr_t base=(uintptr_t)cpu->_pointer_offset;
    if(end>UINT64_C(0x100000000) || base>UINTPTR_MAX-(end-1))abort();
    unsigned char input[16]={0};memcpy(input,(void *)(base+address),size);publish(cpu,op,dest,input);
}
