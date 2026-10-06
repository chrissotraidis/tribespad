#include "llasm_cpu.h"
#include <stdlib.h>
static uint32_t *bit_register(_cpu *c,unsigned selector) {
 switch(selector) {
 case 0:return &c->_eax;case 1:return &c->_ecx;case 2:return &c->_edx;case 3:return &c->_ebx;
 case 4:return &c->_esp;case 5:return &c->_ebp;case 6:return &c->_esi;case 7:return &c->_edi;
 default:abort();
 }
}
/* BSF defines only ZF. For zero input we preserve the architecturally
 * undefined destination; importantly, never call ctz with zero. */
void tb_bsf32(_cpu *c,unsigned destination,uint32_t value) {
 uint32_t *dst=bit_register(c,destination);
 if(value) {*dst=(uint32_t)__builtin_ctz(value);c->_eflags&=~ZF;}
 else c->_eflags|=ZF;
}
/* Intel SDM TZCNT: zero input returns 32 and sets CF; zero output sets ZF.
 * OF/SF/AF/PF are undefined; preserve them and every unrelated CPU field.
 * Source evaluation precedes destination publication, including aliases. */
void tb_tzcnt32(_cpu *c,unsigned destination,uint32_t value) {
 uint32_t *dst=bit_register(c,destination);
 uint32_t result=value?(uint32_t)__builtin_ctz(value):32;
 *dst=result;c->_eflags=(c->_eflags&~(CF|ZF))|(value?0:CF)|(result?0:ZF);
}
/* Register bit indices wrap modulo 32. Other arithmetic flags are undefined;
 * preserve them, and all unrelated flags, while publishing the original bit. */
void tb_btr32(_cpu *c,unsigned destination,uint32_t index) {
 uint32_t *dst=bit_register(c,destination),mask=UINT32_C(1)<<(index&31);
 uint32_t value=*dst;*dst=value&~mask;
 c->_eflags=(c->_eflags&~CF)|((value&mask)?CF:0);
}
void tb_bt32(_cpu *c,uint32_t value,uint32_t index) {
 c->_eflags=(c->_eflags&~CF)|((value>>(index&31))&1u);
}
/* 16-bit compare writes all six arithmetic flags and preserves the rest. */
static uint32_t cmp16_flags(uint32_t flags,uint16_t a,uint16_t b) {
 uint16_t r=(uint16_t)(a-b);
 unsigned p=(unsigned)r&255;p^=p>>4;p^=p>>2;p^=p>>1;
 uint32_t f= (a<b?CF:0) | ((p&1)?0:PF) | ((a^b^r)&AF)
  | (r==0?ZF:0) | ((r&0x8000)?SF:0)
  | (((a^b)&(a^r)&0x8000)?OF:0);
 return (flags&~(CF|PF|AF|ZF|SF|OF))|f;
}
void tb_repne_scasw(CPU) {
 /* No memory access, pointer/count change or flags update when ECX is zero. */
 uint32_t count=ecx,ptr=edi;uint16_t a=(uint16_t)eax;
 uint32_t step=(eflags&DF)?UINT32_C(0xfffffffe):2;
 while(count) {
  const unsigned char *p=REG2PTR(ptr);
  uint16_t b=(uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));
  eflags=cmp16_flags(eflags,a,b);ptr+=step;count--;
  if(a==b)break;
 }
 ecx=count;edi=ptr;
}

/* Reached non-LOCK BTS [ESI],EAX at IFC22 0x10105216. A signed memory
 * bit offset selects a dword, rather than wrapping within the first dword.
 * Unsigned address arithmetic retains the original 32-bit effective address. */
void tb_startup_bts_10105216(_cpu *cpu) {
 uint32_t index=cpu->_eax;
 uint32_t word=(uint32_t)((int32_t)index>>5);
 uint32_t address=cpu->_esi+(word<<2),value;
 unsigned char *p=REG2PTR(address);
 __builtin_memcpy(&value,p,4);
 uint32_t mask=UINT32_C(1)<<(index&31),updated=value|mask;
 __builtin_memcpy(p,&updated,4);
 cpu->_eflags=(cpu->_eflags&~CF)|((value&mask)?CF:0);
}

static void startup_bt32(_cpu *cpu,uint32_t base) {
 uint32_t index=cpu->_eax,word=(uint32_t)((int32_t)index>>5);
 uint32_t address=base+(word<<2),value;
 __builtin_memcpy(&value,REG2PTR(address),4);
 cpu->_eflags=(cpu->_eflags&~CF)|((value>>(index&31))&1u);
}
void tb_startup_bt_1010523b(_cpu *cpu) {startup_bt32(cpu,cpu->_esi);}
void tb_startup_bt_10105269(_cpu *cpu) {startup_bt32(cpu,cpu->_edx);}
