#include "sse_arithmetic.h"
#ifndef LITTLEENDIAN
#define LITTLEENDIAN 1
#endif
#include "softfloat.h"
#include <math.h>
#include <string.h>
/* Exact hardware fast path. For round-to-nearest with finite operands and
   result well inside the normal range, IEEE 754 binary32/binary64 add, sub,
   mul and div produce the same correctly rounded value on AArch64 as on
   x86 SSE, and the only possible status is precision (PE). PE is derived
   from an exact error term (TwoSum, or an FMA residual for mul/div), which is
   representable because no intermediate can underflow or overflow in these
   ranges. Everything else, including any host FPCR rounding or flush mode,
   uses the SoftFloat reference below. */
static int host_nearest(void) {
    /* Probe the host mode with ordinary arithmetic (no inline assembly or
       external call): round-to-nearest-even and no flush-to-zero, else the
       SoftFloat reference is used. Volatile operands prevent folding. */
    volatile float one=1.0f,half=0x1p-24f,three_quarters=0x1.8p-24f,tiny=0x1p-126f,scale=0.5f;
    return one+half==1.0f && -one-half==-1.0f && one+three_quarters==0x1.000002p0f && tiny*scale!=0.0f;
}
static int fast32(uint32_t x){uint32_t e=(x>>23)&0xff;return (e>=48&&e<=200)||!(x<<1);}
static int fast64(uint64_t x){uint64_t e=(x>>52)&0x7ff;return (e>=128&&e<=1900)||!(x<<1);}
/* Zero operands and exact zero results are exact too: no flags, and the
   result sign follows IEEE 754 round to nearest on both x86 and AArch64.
   A zero divisor (divide-by-zero / invalid) still takes the reference. */
static int evaluate_fast(unsigned op,uint64_t a,uint64_t b,uint32_t control,TBSSEResult *out) {
    if((control>>13)&3)return 0;
    if(op>=TB_SSE_MUL32) {
        uint32_t x=(uint32_t)a,y=(uint32_t)b;if(!fast32(x)||!fast32(y)||!host_nearest())return 0;
        float l,r,s,e;memcpy(&l,&x,4);memcpy(&r,&y,4);
        if(op==TB_SSE_ADD32||op==TB_SSE_SUB32) {
            if(op==TB_SSE_SUB32)r=-r;
            s=l+r;float v=s-l;e=(l-(s-v))+(r-v);
        } else if(op==TB_SSE_MUL32){s=l*r;e=fmaf(l,r,-s);}
        else {if(!(y<<1))return 0;s=l/r;e=fmaf(-s,r,l);}
        uint32_t z;memcpy(&z,&s,4);
        if(!fast32(z)||(!(z<<1)&&op!=TB_SSE_ADD32&&op!=TB_SSE_SUB32&&(x<<1)&&(y<<1)))return 0; /* underflow */
        *out=(TBSSEResult){z,e!=0.0f?32u:0u,1};return 1;
    }
    if(!fast64(a)||!fast64(b)||!host_nearest())return 0;
    double l,r,s,e;memcpy(&l,&a,8);memcpy(&r,&b,8);
    if(op==TB_SSE_SUB64)r=-r;
    s=l+r;double v=s-l;e=(l-(s-v))+(r-v);
    uint64_t z;memcpy(&z,&s,8);
    if(!fast64(z))return 0;
    *out=(TBSSEResult){z,e!=0.0?32u:0u,1};return 1;
}
TBSSEResult tb_sse_evaluate_reference(unsigned op,uint64_t a,uint64_t b,uint32_t control);
TBSSEResult tb_sse_evaluate(unsigned op,uint64_t a,uint64_t b,uint32_t control) {
    if(op>TB_SSE_SUB32 || control>0xffff)return (TBSSEResult){0,0,0};
    TBSSEResult fast;if(evaluate_fast(op,a,b,control,&fast))return fast;
    return tb_sse_evaluate_reference(op,a,b,control);
}
TBSSEResult tb_sse_evaluate_reference(unsigned op,uint64_t a,uint64_t b,uint32_t control) {
    if(op>TB_SSE_SUB32 || control>0xffff)return (TBSSEResult){0,0,0};
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
        return (TBSSEResult){(nan_a?a:b)|quiet,
            ((nan_a&&!(a&quiet))||(nan_b&&!(b&quiet)))?1u:0u,1};
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
    return (TBSSEResult){value,flags,1};
}

int tb_sse_arithmetic(unsigned op,uint64_t a,uint64_t b,uint32_t control,
                      uint64_t *result,uint32_t *raised) {
    if(!result||!raised)return 0;
    TBSSEResult candidate=tb_sse_evaluate(op,a,b,control);
    if(!candidate.valid)return 0;
    *result=candidate.value;*raised=candidate.raised;return 1;
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
