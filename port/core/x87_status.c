#include "x87_status.h"
#define IE 1u
#define DE 2u
#define SF 0x40u
#define ES 0x80u
#define C0 0x100u
#define C1 0x200u
#define C2 0x400u
#define C3 0x4000u
#define B 0x8000u
TBExt80 tb_x87_from_binary64(uint64_t bits) {
 unsigned e=(unsigned)(bits>>52)&2047;
 uint64_t frac=bits&UINT64_C(0xfffffffffffff);
 TBExt80 v={0,(uint16_t)((bits>>48)&0x8000)};
 if(e==2047){v.sign_exponent|=0x7fff;v.significand=UINT64_C(0x8000000000000000)|(frac<<11);}
 else if(e){v.sign_exponent|=(uint16_t)(e+16383-1023);v.significand=UINT64_C(0x8000000000000000)|(frac<<11);}
 else if(frac){
  /* Every binary64 subnormal is a normal extended80 value after FLD. */
  unsigned shift=0;while(!(frac&UINT64_C(0x10000000000000))){frac<<=1;shift++;}
  v.significand=frac<<11;v.sign_exponent|=(uint16_t)(16383-1022-shift);
 }
 return v;
}
uint16_t tb_x87_ftst80(TBExt80 v,uint16_t sw,uint16_t cw,int empty) {
 unsigned e=v.sign_exponent&0x7fff;uint64_t m=v.significand;
 unsigned invalid=empty || (e!=0 && !(m>>63)) || (e==0x7fff && m!=UINT64_C(0x8000000000000000));
 unsigned exception=invalid?IE:((e==0 && m)?DE:0);
 uint16_t next=(uint16_t)(sw&~C1);
 if(empty)next|=SF;
 next|=(uint16_t)exception;
 if(exception && !(cw&exception)) {
  /* Preserve condition codes until the unmasked exception is serviced. */
  return (uint16_t)(next|ES|B);
 }
 next&=(uint16_t)~(C0|C2|C3);
 if(invalid)next|=C0|C2|C3;
 else if(!m)next|=C3;
 else if(v.sign_exponent&0x8000)next|=C0;
 return next;
}
