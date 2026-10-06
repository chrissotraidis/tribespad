#include "shift.h"
#include <stdlib.h>
#define CF 1u
#define PF 4u
#define ZF 64u
#define SF 128u
#define OF 2048u
static unsigned parity(uint32_t v) {v&=255;v^=v>>4;v^=v>>2;v^=v>>1;return !(v&1);}
TBShiftResult tb_shift(unsigned op,unsigned width,uint32_t value,uint32_t source,unsigned count,uint32_t flags) {
 if(width!=8 && width!=16 && width!=32)abort();
 if(op>TB_ROR || ((op==TB_SHRD||op==TB_SHLD) && width!=32))abort();
 uint64_t mask=(UINT64_C(1)<<width)-1,sign=UINT64_C(1)<<(width-1);
 uint64_t a=value&mask,r=a;unsigned n=count&31,cf=0;uint32_t defined=0,newflags=0;
 if(!n)return (TBShiftResult){(uint32_t)a,flags};
 if(op==TB_ROL||op==TB_ROR) {
  unsigned rotation=n%width;
  if(rotation)r=op==TB_ROL?((a<<rotation)|(a>>(width-rotation)))&mask:((a>>rotation)|(a<<(width-rotation)))&mask;
  cf=op==TB_ROL?(unsigned)(r&1):(unsigned)!!(r&sign);defined=CF;
 } else {
  defined=PF|ZF|SF;
  if(op==TB_SHL){r=n<width?(a<<n)&mask:0;if(n<width){cf=(unsigned)((a>>(width-n))&1);defined|=CF;}}
  else if(op==TB_SHR){r=n<width?a>>n:0;if(n<width){cf=(unsigned)((a>>(n-1))&1);defined|=CF;}}
  else if(op==TB_SAR) {
   cf=n>=width?(unsigned)!!(a&sign):(unsigned)((a>>(n-1))&1);defined|=CF;
   if(n>=width)r=(a&sign)?mask:0;
   else {r=a>>n;if(a&sign)r|=(mask<<(width-n))&mask;}
  } else if(op==TB_SHRD) { /* 32-bit SHRD: all masked nonzero counts are within the width. */
   r=((a>>n)|((uint64_t)source<<(32-n)))&mask;cf=(unsigned)((a>>(n-1))&1);defined|=CF;
  } else { /* 32-bit SHLD, n is 1..31; no shift by the width. */
   r=((a<<n)|((uint64_t)source>>(32-n)))&mask;cf=(unsigned)((a>>(32-n))&1);defined|=CF;
  }
  if(parity((uint32_t)r))newflags|=PF;if(!r)newflags|=ZF;if(r&sign)newflags|=SF;
 }
 if(cf)newflags|=CF;
 if(n==1) {
  defined|=OF;unsigned of=0;
  if(op==TB_SHL||op==TB_ROL)of=(unsigned)!!(r&sign)^cf;
  else if(op==TB_ROR)of=(unsigned)((r>>(width-1))^(r>>(width-2)))&1;
  else if(op==TB_SHR)of=(unsigned)!!(a&sign);
  else if(op==TB_SHRD||op==TB_SHLD)of=(unsigned)!!((a^r)&sign);
  if(of)newflags|=OF;
 }
 /* AF, multi-bit OF, and out-of-width SHL/SHR CF are architecturally
    undefined. Preserve them; they are never claimed as oracle-verified. */
 return (TBShiftResult){(uint32_t)r,(flags&~defined)|(newflags&defined)};
}
