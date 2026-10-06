#include "llasm_cpu.h"
#include "../core/x87_status.h"
#include <stdlib.h>
#include <string.h>
/* Bridge for SR's current binary64 stack. Extended operations will migrate to
   raw extended80 storage; classification itself already supports that format. */
void tb_x87_ftst(CPU) {
 if(cpu->_st_top>=8)abort();
 uint64_t bits;memcpy(&bits,&cpu->_st[cpu->_st_top],8);
 TBExt80 v=tb_x87_from_binary64(bits);
 if(cpu->_st_extended) {v.significand=bits;v.sign_exponent=cpu->_st_exp[cpu->_st_top];}
 int empty=((cpu->_st_tag>>(2*cpu->_st_top))&3)==3;
 if(cpu->_st_mmx_active) {v.sign_exponent=0xffff;v.significand=bits;}
 uint16_t sw=tb_x87_ftst80(v,(uint16_t)cpu->_st_sw_cond,(uint16_t)cpu->_st_cw,empty);
 cpu->_st_sw_cond=sw;
 /* A Windows x87 exception-dispatch path is still pending. Fail closed instead
    of continuing the game with an unserviced exception. */
 if(sw&0x80)abort();
}
void tb_x87_fclex(CPU){cpu->_st_sw_cond&=~UINT32_C(0x80ff);}
void tb_x87_ffree(CPU,unsigned index){if(index>=8||cpu->_st_top>=8)abort();cpu->_st_tag|=3u<<(2*((cpu->_st_top+index)&7));}
