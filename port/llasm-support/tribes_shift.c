#include "llasm_cpu.h"
#include "../core/shift.h"
#include <stdlib.h>
static uint32_t readreg(_cpu *cpu,unsigned i) {
 switch(i){case 0:return cpu->_eax;case 1:return cpu->_ecx;case 2:return cpu->_edx;case 3:return cpu->_ebx;case 4:return cpu->_esp;case 5:return cpu->_ebp;case 6:return cpu->_esi;case 7:return cpu->_edi;default:abort();}
}
static void writereg(_cpu *cpu,unsigned i,uint32_t v) {
 switch(i){case 0:cpu->_eax=v;break;case 1:cpu->_ecx=v;break;case 2:cpu->_edx=v;break;case 3:cpu->_ebx=v;break;case 4:cpu->_esp=v;break;case 5:cpu->_ebp=v;break;case 6:cpu->_esi=v;break;case 7:cpu->_edi=v;break;default:abort();}
}
void tb_shift_reg(CPU,unsigned op,unsigned selector,unsigned width,uint32_t source,unsigned count) {
 unsigned high=selector>=8,idx=selector&7,offset=high?8:0;
 if(selector>=12 || (high&&width!=8))abort();
 uint32_t original=readreg(cpu,idx);TBShiftResult r=tb_shift(op,width,original>>offset,source,count,eflags);
 uint32_t mask=(uint32_t)((UINT64_C(1)<<width)-1)<<offset;
 writereg(cpu,idx,(original&~mask)|(r.value<<offset));eflags=r.flags;
}
void tb_shift_mem(CPU,unsigned op,uint32_t address,unsigned width,uint32_t source,unsigned count) {
 if(width!=8&&width!=16&&width!=32)abort();
 unsigned char *p=REG2PTR(address);uint32_t v=0;
 for(unsigned i=0;i<width/8;i++)v|=(uint32_t)p[i]<<(8*i);
 TBShiftResult r=tb_shift(op,width,v,source,count,eflags);
 for(unsigned i=0;i<width/8;i++)p[i]=(unsigned char)(r.value>>(8*i));eflags=r.flags;
}

/* BMI2 IA-32 shifts mask counts to five bits and preserve every flag. */
void tb_bmi_shift(CPU,unsigned op,unsigned dest,unsigned source,unsigned countreg) {
 if(op>1)abort();uint32_t value=readreg(cpu,source);unsigned count=readreg(cpu,countreg)&31;
 writereg(cpu,dest,op?value>>count:value<<count);
}
void tb_bmi_shiftmem(CPU,unsigned op,unsigned dest,uint32_t address,unsigned countreg) {
 if(op>1)abort();const unsigned char *p=REG2PTR(address);uint32_t value=0;unsigned count=readreg(cpu,countreg)&31;
 for(unsigned j=0;j<4;j++)value|=(uint32_t)p[j]<<(8*j);
 writereg(cpu,dest,op?value>>count:value<<count);
}
