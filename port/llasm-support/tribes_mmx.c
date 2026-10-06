#include "llasm_cpu.h"
#include "../core/mmx.h"
#include <string.h>
#include <stdlib.h>
/* MM registers alias physical x87 data registers, independent of TOP. The
   inherited SR runtime does not yet emulate x87 tags/exceptions. */
static uint64_t get(_cpu *cpu,unsigned i) {
 uint64_t v;if(i>=8)abort();memcpy(&v,&cpu->_st[i],8);return v;
}
static void put(_cpu *cpu,unsigned i,uint64_t v) {
 if(i>=8)abort();memcpy(&cpu->_st[i],&v,8);cpu->_st_top=0;cpu->_st_tag=0;cpu->_st_mmx_active=1;
 if(cpu->_st_extended)for(unsigned n=0;n<8;n++)cpu->_st_exp[n]=0xffff;
}
uint32_t tb_mmx_low(CPU,unsigned i){uint64_t v=get(cpu,i);put(cpu,i,v);return (uint32_t)v;}
void tb_mmx_apply(CPU,unsigned op,unsigned i,uint32_t lo,uint32_t hi) {
 put(cpu,i,tb_mmx_eval(op,get(cpu,i),(uint64_t)lo|((uint64_t)hi<<32)));
}
void tb_mmx_emms(CPU){cpu->_st_tag=0xffff;cpu->_st_mmx_active=0;}

void tb_mmx_regop(CPU,unsigned op,unsigned dst,unsigned src) {
 put(cpu,dst,tb_mmx_eval(op,get(cpu,dst),get(cpu,src)));
}
void tb_mmx_store(CPU,unsigned i,uint32_t address,unsigned width) {
 uint64_t v=get(cpu,i);cpu->_st_top=0;cpu->_st_tag=0;cpu->_st_mmx_active=1;
 if(cpu->_st_extended)for(unsigned n=0;n<8;n++)cpu->_st_exp[n]=0xffff;
 if(width!=32&&width!=64)abort();
 /* Store bytes explicitly: guest memory is little endian even on another host. */
 unsigned char *p=REG2PTR(address);
 for(unsigned j=0;j<width/8;j++)p[j]=(unsigned char)(v>>(8*j));
}
