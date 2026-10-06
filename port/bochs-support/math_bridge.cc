/* TribesPad adapter around pinned, pure x87 software math. */
#include "softfloat3e/include/softfloat.h"
#include "fpu_trans.h"
#include "../core/bochs_math.h"
const floatx80 Const_Z = {.signif=0, .signExp=0};
const floatx80 Const_1 = {.signif=UINT64_C(0x8000000000000000), .signExp=0x3fff};
extern "C" int tb_bo_store_integer(unsigned width,TBExt80 value,uint16_t control,
                                   int64_t *result,uint16_t *flags) {
 softfloat_status_t status={};
 status.softfloat_roundingMode=(control>>10)&3;
 status.softfloat_exceptionMasks=control&63;
 status.extF80_roundingPrecision=80;
 floatx80 x={.signif=value.significand,.signExp=value.sign_exponent};
 if(width==16)*result=extF80_to_i16(x,&status);
 else if(width==32)*result=extF80_to_i32(x,&status);
 else if(width==64)*result=extF80_to_i64(x,&status);
 else return 0;
 *flags=status.softfloat_exceptionFlags;
 return 1;
}
extern "C" int tb_bo_store_real(unsigned width,TBExt80 value,uint16_t control,
                                uint64_t *bits,uint16_t *flags) {
 softfloat_status_t status={};
 status.softfloat_roundingMode=(control>>10)&3;
 status.softfloat_exceptionMasks=control&63;
 status.extF80_roundingPrecision=80;
 floatx80 x={.signif=value.significand,.signExp=value.sign_exponent};
 if(width==32)*bits=extF80_to_f32(x,&status);
 else if(width==64)*bits=extF80_to_f64(x,&status);
 else return 0;
 *flags=status.softfloat_exceptionFlags;
 return 1;
}
extern "C" int tb_bo_math(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,
                          TBExt80 *result,uint16_t *flags,uint64_t *quotient) {
 softfloat_status_t status={};
 status.softfloat_roundingMode=(control>>10)&3;
 status.softfloat_exceptionMasks=control&63;
 status.extF80_roundingPrecision=80;
 floatx80 x={.signif=a.significand,.signExp=a.sign_exponent};
 floatx80 y={.signif=b.significand,.signExp=b.sign_exponent};
 floatx80 z=x;int rc=0;Bit64u q=0;
 switch(op){
  case 0:z=f2xm1(x,status);break;
  case 1:rc=fsin(z,status);break;
  case 2:rc=fcos(z,status);break;
  case 3:rc=fptan(z,status);break;
  case 4:z=fyl2x(x,y,status);break;
  case 5:z=fpatan(x,y,status);break;
  case 6:rc=floatx80_remainder(x,y,z,q,&status);break;
  case 7:z=extF80_scale(x,y,&status);break;
  default:return -2;
 }
 *result={z.signif,z.signExp};*flags=status.softfloat_exceptionFlags;*quotient=q;
 return rc;
}
extern "C" int tb_bo_arithmetic(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,
                                TBExt80 *result,uint16_t *flags) {
 unsigned pc=(control>>8)&3;
 if((pc==1&&op!=TB_BO_ROUND_INTEGER)||op>TB_BO_ROUND_INTEGER)return 0;
 softfloat_status_t status={};
 status.softfloat_roundingMode=(control>>10)&3;
 status.softfloat_exceptionMasks=control&63;
 status.extF80_roundingPrecision=pc==0?32:pc==2?64:80;
 floatx80 x={.signif=a.significand,.signExp=a.sign_exponent};
 floatx80 y={.signif=b.significand,.signExp=b.sign_exponent},z;
 switch(op){
  case 0:z=extF80_add(x,y,&status);break;
  case 1:z=extF80_sub(x,y,&status);break;
  case 2:z=extF80_mul(x,y,&status);break;
  case 3:z=extF80_div(x,y,&status);break;
  case 4:z=extF80_sqrt(x,&status);break;
  default:z=extF80_roundToInt(x,&status);break;
 }
 *result={z.signif,z.signExp};*flags=status.softfloat_exceptionFlags;
 return 1;
}
