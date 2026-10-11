/* Exact hardware path for x87 FADD/FSUB/FMUL/FDIV/FSQRT.
 *
 * When both operands are exactly representable as binary64 and the true
 * result is exactly representable as binary64 (proved by an exact error term:
 * TwoSum for add/sub, an FMA residual for mul/div/sqrt), every correct x87
 * implementation returns that exact value at any precision control, with no
 * exception flags (an exact zero takes the round-to-nearest sign). Operands and results are kept far from the binary64 range
 * limits so no error term can underflow or overflow. Anything else, including
 * NaNs, infinities, denormals,
 * unnormals and directed rounding, returns 0 so the caller uses SoftFloat.
 */
#ifndef TRIBESPAD_X87_EXACT_H
#define TRIBESPAD_X87_EXACT_H
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "ext80.h"

static inline int x87_exact_host_probe(void) {
    /* Round-to-nearest-even without flush-to-zero, probed with arithmetic. */
    volatile double one = 1.0, half = 0x1p-53, three = 0x1.8p-53, tiny = 0x1p-1022, scale = 0.5;
    return one + half == 1.0 && -one - half == -1.0 && one + three == 0x1.0000000000001p0 && tiny * scale != 0.0;
}
/* The host mode is fixed for the process; probe once (a racing first probe
   computes the same answer). */
static inline int x87_exact_host_nearest(void) {
    static volatile int cached = -1;
    int value = cached;
    if (value < 0) cached = value = x87_exact_host_probe();
    return value;
}
/* Extended -> binary64, exactly; 1 on success. Zero or normal within +-1000. */
static inline int x87_exact_to_double(TBExt80 v, double *out) {
    uint64_t sign = (uint64_t)(v.sign_exponent >> 15), e = v.sign_exponent & 0x7fff, bits;
    if (!e && !v.significand) { bits = sign << 63; memcpy(out, &bits, 8); return 1; }
    if (!(v.significand >> 63) || (v.significand & 0x7ff)) return 0;
    int64_t unbiased = (int64_t)e - 16383;
    if (unbiased < -1000 || unbiased > 1000) return 0;
    bits = sign << 63 | (uint64_t)(unbiased + 1023) << 52 | ((v.significand << 1) >> 12);
    memcpy(out, &bits, 8); return 1;
}
/* Nonzero binary64 within +-1000 -> extended, exactly; 1 on success. */
static inline int x87_exact_from_double(double d, TBExt80 *out) {
    uint64_t bits; memcpy(&bits, &d, 8);
    int64_t e = (int64_t)((bits >> 52) & 0x7ff) - 1023;
    if (!(bits << 1) || e < -1000 || e > 1000) return 0;
    out->significand = UINT64_C(1) << 63 | (bits & UINT64_C(0xfffffffffffff)) << 11;
    out->sign_exponent = (uint16_t)((bits >> 63) << 15 | (uint64_t)(e + 16383));
    return 1;
}
/* op: 0 add, 1 sub, 2 mul, 3 div, 4 sqrt (a only). */
static inline int x87_exact_arithmetic(unsigned op, TBExt80 a, TBExt80 b, uint16_t control, TBExt80 *result) {
    if (op > 4 || (control & 0x0c00) || ((control >> 8) & 3) == 1) return 0;
    double x, y = 0, z, err;
    if (!x87_exact_to_double(a, &x) || (op != 4 && !x87_exact_to_double(b, &y))) return 0;
    if (!x87_exact_host_nearest()) return 0;
    /* Multiply/divide operands stay within +-500 so a product or quotient can
       never underflow; then a zero result means a zero operand. */
    if ((op == 2 || op == 3) && (fabs(x) > 0x1p500 || fabs(y) > 0x1p500 ||
        (x != 0 && fabs(x) < 0x1p-500) || (y != 0 && fabs(y) < 0x1p-500))) return 0;
    switch (op) {
    case 0: case 1: {
        if (op == 1) y = -y;
        z = x + y; double v = z - x; err = (x - (z - v)) + (y - v); break;
    }
    case 2: z = x * y; err = fma(x, y, -z); break;
    case 3: if (y == 0) return 0; z = x / y; err = fma(-z, y, x); break;
    default: if (!(x > 0)) return 0; z = sqrt(x); err = fma(-z, z, x); break;
    }
    if (err != 0) return 0;
    /* Exact zero results (x-x, 0*x, 0/x) have the same sign as on x87 under
       round to nearest. */
    if (z == 0) { uint64_t bits; memcpy(&bits, &z, 8); result->significand = 0; result->sign_exponent = (uint16_t)((bits >> 63) << 15); return 1; }
    /* PC=24 (control bits 9:8 == 0) also needs the result to fit 24 bits. */
    TBExt80 r; if (!x87_exact_from_double(z, &r)) return 0;
    if (((control >> 8) & 3) == 0 && (r.significand & UINT64_C(0xffffffffff))) return 0;
    if (((control >> 8) & 3) == 2 && (r.significand & 0x7ff)) return 0;
    *result = r; return 1;
}
#endif
