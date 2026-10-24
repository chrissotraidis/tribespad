/* x87 helpers Starsiege: Tribes needs that Tribes 2 never reached. Kept apart
 * from port/llasm-support/tribes_float80.c so Tribes 2's reviewed sources and
 * proof receipts are unchanged. Same register model and Bochs software math. */
#include "../llasm-support/llasm_cpu.h"
#include "../core/ext80.h"
#include "../core/bochs_math.h"
#include <stdlib.h>
#include <string.h>

static const TBExt80 indefinite={UINT64_C(0xc000000000000000),0xffff};
static unsigned slot(_cpu *cpu,unsigned n){if(n>=8||cpu->_st_top>=8||!cpu->_st_extended)abort();return (cpu->_st_top+n)&7;}
static unsigned tag(TBExt80 v){unsigned e=v.sign_exponent&0x7fff;if(!e&&!v.significand)return 1;if(!e||e==0x7fff||!(v.significand>>63))return 2;return 0;}
/* Unmasked x87 exceptions are not modeled: stop rather than continue wrongly. */
static void exception(_cpu *cpu,unsigned flags){cpu->_st_sw_cond|=flags;if(flags&(~cpu->_st_cw)&63){cpu->_st_sw_cond|=0x8080;abort();}}
static TBExt80 read_st(_cpu *cpu,unsigned n){
 unsigned i=slot(cpu,n);TBExt80 v;memcpy(&v.significand,&cpu->_st[i],8);v.sign_exponent=cpu->_st_exp[i];
 if(((cpu->_st_tag>>(2*i))&3)==3){cpu->_st_sw_cond&=~0x200u;exception(cpu,0x41);return indefinite;}
 return v;
}
static void write_st(_cpu *cpu,unsigned n,TBExt80 v){unsigned i=slot(cpu,n);memcpy(&cpu->_st[i],&v.significand,8);cpu->_st_exp[i]=v.sign_exponent;cpu->_st_tag=(cpu->_st_tag&~(3u<<(2*i)))|(tag(v)<<(2*i));}
static void push(_cpu *cpu,TBExt80 v){unsigned i=slot(cpu,7);if(((cpu->_st_tag>>(2*i))&3)!=3){cpu->_st_sw_cond|=0x200;exception(cpu,0x41);v=indefinite;}else cpu->_st_sw_cond&=~0x200u;cpu->_st_top=i;write_st(cpu,0,v);}

/* FSINCOS: ST0 <- sin(ST0), then push cos, so ST1 = sine and ST0 = cosine.
   Out of range (|ST0| >= 2^63) sets C2 and leaves the stack unchanged. */
void x87_fsincos_void(_cpu *cpu){
 TBExt80 a=read_st(cpu,0),s,c;uint16_t fs=0,fc=0;uint64_t q;
 if(tb_bo_math(1,a,indefinite,(uint16_t)cpu->_st_cw,&s,&fs,&q)==-1){cpu->_st_sw_cond|=0x400;return;}
 (void)tb_bo_math(2,a,indefinite,(uint16_t)cpu->_st_cw,&c,&fc,&q);
 cpu->_st_sw_cond&=~0x600u;exception(cpu,fs|fc);
 write_st(cpu,0,s);push(cpu,c);
}

