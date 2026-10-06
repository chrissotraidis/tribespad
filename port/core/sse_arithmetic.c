#include "sse_arithmetic.h"
#ifndef LITTLEENDIAN
#define LITTLEENDIAN 1
#endif
#include "softfloat.h"
int tb_sse_arithmetic(unsigned op,uint64_t a,uint64_t b,uint32_t control,
                      uint64_t *result,uint32_t *raised) {
    if(op>TB_SSE_SUB32 || control>0xffff || !result || !raised)return 0;
    const int single=op>=TB_SSE_MUL32;
    const uint64_t sign=single?UINT64_C(0x80000000):UINT64_C(0x8000000000000000);
    const uint64_t infinity=single?UINT64_C(0x7f800000):UINT64_C(0x7ff0000000000000);
    const uint64_t quiet=single?UINT64_C(0x00400000):UINT64_C(0x0008000000000000);
    const uint64_t normal_min=single?UINT64_C(0x00800000):UINT64_C(0x0010000000000000);
    if(single){a=(uint32_t)a;b=(uint32_t)b;}
    uint64_t abs_a=a&~sign,abs_b=b&~sign;
    const int nan_a=abs_a>infinity,nan_b=abs_b>infinity;
    /* Intel SSE prioritizes the first NaN operand. The pinned 8086 library
       has a different two-NaN payload policy, so handle this before entry.
       SUB preserves the source NaN's sign/payload rather than negating it. */
    if(nan_a||nan_b) {
        *result=(nan_a?a:b)|quiet;
        *raised=((nan_a&&!(a&quiet))||(nan_b&&!(b&quiet)))?1:0;
        return 1;
    }
    unsigned flags=0;
    int den_a=abs_a&&abs_a<normal_min,den_b=abs_b&&abs_b<normal_min;
    if(control&0x40) {
        if(den_a)a&=sign;
        if(den_b)b&=sign;
    } else if(den_a||den_b)flags|=2;
    /* Divide-by-zero precomputation has priority over a denormal numerator.
       NaNs were handled first; DAZ has already converted source denormals. */
    if(op==TB_SSE_DIV32 && !(b&~sign) && (a&~sign) && (a&~sign)<infinity)flags&=~2u;
    /* SoftFloat's configuration is TLS. Restore every field even when a
       result has exceptions; x87 and another guest context must not inherit
       this instruction's rounding/tininess/status settings. */
    uint_fast8_t saved_round=softfloat_roundingMode;
    uint_fast8_t saved_tiny=softfloat_detectTininess;
    uint_fast8_t saved_flags=softfloat_exceptionFlags;
    uint_fast8_t saved_precision=extF80_roundingPrecision;
    softfloat_roundingMode=(uint_fast8_t[]){softfloat_round_near_even,
        softfloat_round_min,softfloat_round_max,softfloat_round_minMag}[(control>>13)&3];
    softfloat_detectTininess=softfloat_tininess_afterRounding;
    softfloat_exceptionFlags=0;
    uint64_t value;
    if(single) {
        float32_t left={(uint32_t)a},right={(uint32_t)b};
        value=(op==TB_SSE_MUL32?f32_mul(left,right):op==TB_SSE_DIV32?f32_div(left,right):op==TB_SSE_SUB32?f32_sub(left,right):f32_add(left,right)).v;
    }
    else if(op==TB_SSE_ADD64)value=f64_add((float64_t){a},(float64_t){b}).v;
    else value=f64_sub((float64_t){a},(float64_t){b}).v;
    unsigned sf=softfloat_exceptionFlags;
    flags|=((sf&softfloat_flag_invalid)?1:0)
         |((sf&softfloat_flag_infinite)?4:0)
         |((sf&softfloat_flag_overflow)?8:0)
         |((sf&softfloat_flag_underflow)?16:0)
         |((sf&softfloat_flag_inexact)?32:0);
    softfloat_roundingMode=saved_round;softfloat_detectTininess=saved_tiny;
    softfloat_exceptionFlags=saved_flags;extF80_roundingPrecision=saved_precision;
    uint64_t abs_result=value&~sign;
    /* A working-precision tiny result can round to minimum normal while UE
       remains set. FTZ handles it as well as exact subnormal results. */
    if((control&0x8000)&&((flags&16)||(abs_result&&abs_result<normal_min))) {
        value&=sign;flags|=16|32;
    }
    *result=value;*raised=flags;return 1;
}

int tb_sse_sqrt64(uint64_t source,uint32_t control,uint64_t *result,uint32_t *raised) {
    if(control>0xffff || !result || !raised)return 0;
    const uint64_t sign=UINT64_C(0x8000000000000000),quiet=UINT64_C(0x0008000000000000);
    uint64_t magnitude=source&~sign;unsigned flags=0;
    if(magnitude>UINT64_C(0x7ff0000000000000)) {
        *result=source|quiet;*raised=(source&quiet)?0:1;return 1;
    }
    if(magnitude && magnitude<UINT64_C(0x0010000000000000) && (control&0x40)) {
        source&=sign;magnitude=0;
    }
    /* Invalid has priority over denormal status for negative nonzero inputs.
       DAZ runs first, allowing negative subnormals to become negative zero. */
    if((source&sign)&&magnitude) {
        *result=UINT64_C(0xfff8000000000000);*raised=1;return 1;
    }
    if(!magnitude || magnitude==UINT64_C(0x7ff0000000000000)) {
        *result=source;*raised=0;return 1;
    }
    if(magnitude<UINT64_C(0x0010000000000000))flags=2;
    uint_fast8_t saved_round=softfloat_roundingMode,saved_tiny=softfloat_detectTininess;
    uint_fast8_t saved_flags=softfloat_exceptionFlags,saved_precision=extF80_roundingPrecision;
    softfloat_roundingMode=(uint_fast8_t[]){softfloat_round_near_even,
        softfloat_round_min,softfloat_round_max,softfloat_round_minMag}[(control>>13)&3];
    softfloat_detectTininess=softfloat_tininess_afterRounding;softfloat_exceptionFlags=0;
    uint64_t value=f64_sqrt((float64_t){source}).v;
    unsigned sf=softfloat_exceptionFlags;
    flags|=((sf&softfloat_flag_invalid)?1:0)|((sf&softfloat_flag_inexact)?32:0);
    softfloat_roundingMode=saved_round;softfloat_detectTininess=saved_tiny;
    softfloat_exceptionFlags=saved_flags;extF80_roundingPrecision=saved_precision;
    /* A positive nonzero binary64 square root is normal even for the minimum
       subnormal input. FTZ cannot alter it; overflow/underflow cannot occur. */
    *result=value;*raised=flags;return 1;
}

int tb_sse_sqrt32(uint32_t source,uint32_t control,uint64_t *result,uint32_t *raised) {
    if(control>0xffff || !result || !raised)return 0;
    const uint32_t sign=UINT32_C(0x80000000),quiet=UINT32_C(0x00400000);
    uint32_t magnitude=source&~sign;unsigned flags=0;
    if(magnitude>UINT32_C(0x7f800000)) {
        *result=source|quiet;*raised=(source&quiet)?0:1;return 1;
    }
    if(magnitude && magnitude<UINT32_C(0x00800000) && (control&0x40)) {
        source&=sign;magnitude=0;
    }
    /* Invalid has priority over denormal status for negative nonzero inputs.
       DAZ runs first, allowing negative subnormals to become negative zero. */
    if((source&sign)&&magnitude) {
        *result=UINT32_C(0xffc00000);*raised=1;return 1;
    }
    if(!magnitude || magnitude==UINT32_C(0x7f800000)) {
        *result=source;*raised=0;return 1;
    }
    if(magnitude<UINT32_C(0x00800000))flags=2;
    uint_fast8_t saved_round=softfloat_roundingMode,saved_tiny=softfloat_detectTininess;
    uint_fast8_t saved_flags=softfloat_exceptionFlags,saved_precision=extF80_roundingPrecision;
    softfloat_roundingMode=(uint_fast8_t[]){softfloat_round_near_even,
        softfloat_round_min,softfloat_round_max,softfloat_round_minMag}[(control>>13)&3];
    softfloat_detectTininess=softfloat_tininess_afterRounding;softfloat_exceptionFlags=0;
    uint64_t value=f32_sqrt((float32_t){source}).v;
    unsigned sf=softfloat_exceptionFlags;
    flags|=((sf&softfloat_flag_invalid)?1:0)|((sf&softfloat_flag_inexact)?32:0);
    softfloat_roundingMode=saved_round;softfloat_detectTininess=saved_tiny;
    softfloat_exceptionFlags=saved_flags;extF80_roundingPrecision=saved_precision;
    /* A positive nonzero binary32 square root is normal even for the minimum
       subnormal input. FTZ cannot alter it; overflow/underflow cannot occur. */
    *result=value;*raised=flags;return 1;
}

int tb_sse_multiply64(uint64_t a,uint64_t b,uint32_t control,uint64_t *result,uint32_t *raised) {
    if(control>0xffff || !result || !raised)return 0;
    const uint64_t sign=UINT64_C(0x8000000000000000),infinity=UINT64_C(0x7ff0000000000000),quiet=UINT64_C(0x0008000000000000),normal_min=UINT64_C(0x0010000000000000);
    uint64_t aa=a&~sign,bb=b&~sign;int na=aa>infinity,nb=bb>infinity;
    if(na||nb){*result=(na?a:b)|quiet;*raised=((na&&!(a&quiet))||(nb&&!(b&quiet)))?1:0;return 1;}
    unsigned flags=0;int da=aa&&aa<normal_min,db=bb&&bb<normal_min;
    if(control&0x40){if(da)a&=sign;if(db)b&=sign;}else if(da||db)flags=2;
    uint_fast8_t saved_round=softfloat_roundingMode,saved_tiny=softfloat_detectTininess,saved_flags=softfloat_exceptionFlags,saved_precision=extF80_roundingPrecision;
    softfloat_roundingMode=(uint_fast8_t[]){softfloat_round_near_even,softfloat_round_min,softfloat_round_max,softfloat_round_minMag}[(control>>13)&3];
    softfloat_detectTininess=softfloat_tininess_afterRounding;softfloat_exceptionFlags=0;
    uint64_t value=f64_mul((float64_t){a},(float64_t){b}).v;unsigned sf=softfloat_exceptionFlags;
    flags|=((sf&softfloat_flag_invalid)?1:0)|((sf&softfloat_flag_overflow)?8:0)|((sf&softfloat_flag_underflow)?16:0)|((sf&softfloat_flag_inexact)?32:0);
    softfloat_roundingMode=saved_round;softfloat_detectTininess=saved_tiny;softfloat_exceptionFlags=saved_flags;extF80_roundingPrecision=saved_precision;
    uint64_t magnitude=value&~sign;
    if((control&0x8000)&&((flags&16)||(magnitude&&magnitude<normal_min))){value&=sign;flags|=48;}
    *result=value;*raised=flags;return 1;
}

int tb_sse_divide64(uint64_t a,uint64_t b,uint32_t control,uint64_t *result,uint32_t *raised) {
    if(control>0xffff || !result || !raised)return 0;
    const uint64_t sign=UINT64_C(0x8000000000000000),infinity=UINT64_C(0x7ff0000000000000),quiet=UINT64_C(0x0008000000000000),normal_min=UINT64_C(0x0010000000000000);
    uint64_t aa=a&~sign,bb=b&~sign;int na=aa>infinity,nb=bb>infinity;
    if(na||nb){*result=(na?a:b)|quiet;*raised=((na&&!(a&quiet))||(nb&&!(b&quiet)))?1:0;return 1;}
    unsigned flags=0;int da=aa&&aa<normal_min,db=bb&&bb<normal_min;
    if(control&0x40){if(da)a&=sign;if(db)b&=sign;}else if(da||db)flags=2;
    if(!(b&~sign) && (a&~sign) && (a&~sign)<infinity)flags&=~2u;
    uint_fast8_t saved_round=softfloat_roundingMode,saved_tiny=softfloat_detectTininess,saved_flags=softfloat_exceptionFlags,saved_precision=extF80_roundingPrecision;
    softfloat_roundingMode=(uint_fast8_t[]){softfloat_round_near_even,softfloat_round_min,softfloat_round_max,softfloat_round_minMag}[(control>>13)&3];
    softfloat_detectTininess=softfloat_tininess_afterRounding;softfloat_exceptionFlags=0;
    uint64_t value=f64_div((float64_t){a},(float64_t){b}).v;unsigned sf=softfloat_exceptionFlags;
    flags|=((sf&softfloat_flag_invalid)?1:0)|((sf&softfloat_flag_infinite)?4:0)|((sf&softfloat_flag_overflow)?8:0)|((sf&softfloat_flag_underflow)?16:0)|((sf&softfloat_flag_inexact)?32:0);
    softfloat_roundingMode=saved_round;softfloat_detectTininess=saved_tiny;softfloat_exceptionFlags=saved_flags;extF80_roundingPrecision=saved_precision;
    uint64_t magnitude=value&~sign;
    if((control&0x8000)&&((flags&16)||(magnitude&&magnitude<normal_min))){value&=sign;flags|=48;}
    *result=value;*raised=flags;return 1;
}
