#include "ext80.h"
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error TribesPad SoftFloat adapter currently requires a little-endian target
#endif
#ifndef LITTLEENDIAN
#define LITTLEENDIAN 1
#endif
#include "softfloat.h"

static int unsupported(TBExt80 v) {
    return (v.sign_exponent & 0x7fff) && !(v.significand >> 63);
}
static extFloat80_t canonical(TBExt80 v) {
    /* Intel pseudo-denormals represent the corresponding exponent-one value. */
    if (!(v.sign_exponent & 0x7fff) && (v.significand >> 63)) v.sign_exponent |= 1;
    extFloat80_t x = {.signif=v.significand, .signExp=v.sign_exponent};
    return x;
}
int tb_ext80_arithmetic(unsigned op, TBExt80 a, TBExt80 b, uint16_t control,
                        TBExt80 *result, uint16_t *exceptions) {
    unsigned pc = (control >> 8) & 3;
    if (pc == 1 || op > TB_EXT_SQRT) return 0;
    if (unsupported(a) || (op != TB_EXT_SQRT && unsupported(b))) {
        *result = (TBExt80){UINT64_C(0xc000000000000000),0xffff};
        *exceptions = 1;
        return 1;
    }
    /* Preserve the caller's TLS configuration; no process-global FP mode. */
    uint_fast8_t rm=softfloat_roundingMode, prec=extF80_roundingPrecision;
    uint_fast8_t flags=softfloat_exceptionFlags, tininess=softfloat_detectTininess;
    softfloat_roundingMode = (uint_fast8_t[]){softfloat_round_near_even,
        softfloat_round_min,softfloat_round_max,softfloat_round_minMag}[(control>>10)&3];
    extF80_roundingPrecision = pc==0 ? 32 : pc==2 ? 64 : 80;
    softfloat_exceptionFlags = 0;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    extFloat80_t x=canonical(a), y=canonical(b), z;
    switch (op) {
        case TB_EXT_ADD: z=extF80_add(x,y); break;
        case TB_EXT_SUB: z=extF80_sub(x,y); break;
        case TB_EXT_MUL: z=extF80_mul(x,y); break;
        case TB_EXT_DIV: z=extF80_div(x,y); break;
        default: z=extF80_sqrt(x); break;
    }
    *result = (TBExt80){z.signif,z.signExp};
    unsigned f=softfloat_exceptionFlags;
    *exceptions = ((f & softfloat_flag_invalid) ? 1 : 0)
        | ((f & softfloat_flag_infinite) ? 4 : 0)
        | ((f & softfloat_flag_overflow) ? 8 : 0)
        | ((f & softfloat_flag_underflow) ? 16 : 0)
        | ((f & softfloat_flag_inexact) ? 32 : 0);
    if ((!(a.sign_exponent & 0x7fff) && a.significand)
        || (op!=TB_EXT_SQRT && !(b.sign_exponent & 0x7fff) && b.significand))
        *exceptions |= 2;
    softfloat_roundingMode=rm;
    extF80_roundingPrecision=prec;
    softfloat_exceptionFlags=flags;
    softfloat_detectTininess=tininess;
    return 1;
}
