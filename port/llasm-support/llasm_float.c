// part of static recompiler -- do not edit

/**
 *
 *  Copyright (C) 2019-2026 Roman Pauer
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

#include <string.h>
#include <stdlib.h>
#include "llasm_cpu.h"
#if !defined(__USE_ISOC99)
    #define __USE_ISOC99
#endif
#if !defined(_USE_MATH_DEFINES)
    #define _USE_MATH_DEFINES
#endif
#include <math.h>

#if defined(__SSE2__)
    #include <emmintrin.h>
#endif

#if defined(_MSC_VER)

#undef BIG_ENDIAN_FLOAT_WORD_ORDER
#undef BIG_ENDIAN_BYTE_ORDER

#elif defined(__BYTE_ORDER__)

#if (defined(__FLOAT_WORD_ORDER__) && (__FLOAT_WORD_ORDER__ == __ORDER_BIG_ENDIAN__)) || (!defined(__FLOAT_WORD_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
#define BIG_ENDIAN_FLOAT_WORD_ORDER
#else
#undef BIG_ENDIAN_FLOAT_WORD_ORDER
#endif

#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define BIG_ENDIAN_BYTE_ORDER
#else
#undef BIG_ENDIAN_BYTE_ORDER
#endif

#else

#include <endian.h>
#if (__FLOAT_WORD_ORDER == __BIG_ENDIAN)
#define BIG_ENDIAN_FLOAT_WORD_ORDER
#else
#undef BIG_ENDIAN_FLOAT_WORD_ORDER
#endif

#if (__BYTE_ORDER == __BIG_ENDIAN)
#define BIG_ENDIAN_BYTE_ORDER
#else
#undef BIG_ENDIAN_BYTE_ORDER
#endif

#endif

typedef union {
    float f;
    int32_t i;
} float_int;

typedef union {
    double d;
    struct {
#ifdef BIG_ENDIAN_FLOAT_WORD_ORDER
        uint32_t high;
        uint32_t low;
#else
        uint32_t low;
        uint32_t high;
#endif
    };
} double_int;

typedef union {
    int64_t i;
    struct {
#ifdef BIG_ENDIAN_BYTE_ORDER
        uint32_t high;
        uint32_t low;
#else
        uint32_t low;
        uint32_t high;
#endif
    };
} int_int;

typedef struct {
    uint32_t low;
    uint32_t high;
} le_int;


const static double const_0_0 = 0.0;
const static double const_1_0 = 1.0;
const static double const_lg2 = 0.30102999566398119521; // log10l(2.0l)
const static double const_ln2 = M_LN2;

/* The binary64 reference helpers cannot consume the extended runtime's raw
   significands. Mixing the two numeric stacks must fail visibly. */
static inline double *legacy_st(_cpu *cpu)
{
    if (cpu->_st_extended) abort();
    return cpu->_st;
}
#define st legacy_st(cpu)
#define st_result cpu->_st_result
/* HaloPad: results wider than a register (64-bit integers, doubles) are returned by address.
   In HaloPad's original-address model the CPU state is not in guest memory, so
   halopad_x87_result (port/runtime) copies st_result to a per-thread guest slot and returns
   that slot's guest address; SR's pointer-offset builds keep PTR2REG (weak default). */
EXTERNC uint32_t halopad_x87_result(_cpu *cpu, const int64_t *value);
#define X87_RESULT() halopad_x87_result(cpu, (const int64_t *)&(st_result))

#define st_top cpu->_st_top
#define st_sw_cond cpu->_st_sw_cond
#define st_cw cpu->_st_cw

#define X87_CF 0x0100
#define X87_ZF 0x4000

#define X87_C0 0x0100
#define X87_C1 0x0200
#define X87_C2 0x0400
#define X87_C3 0x4000

#define X87_CX 0x4700

#define X87_RC_SHIFT 10

#define CLEAR_X87_FLAG(x) { st_sw_cond &= ~(x); }

#define CLEAR_X87_FLAGS { st_sw_cond &= ~X87_CX; }

#define X87_CMP(x, y)  { \
    if ( (x) < (y) ) { \
        st_sw_cond = X87_CF; \
    } else if ( (x) == (y) ) { \
        st_sw_cond = X87_ZF; \
    } else { \
        st_sw_cond = 0; \
    } \
}

#define ST(i) st[(st_top+i) & 7]
#define ST0 st[st_top]
#define ST1 ST(1)


/* TribesPad: track empty/nonempty physical registers through stack changes.
   Occupied-target pushes require a future stack-overflow dispatcher. */
#define PUSH_REGS { unsigned next_top=(st_top+7)&7; if(((cpu->_st_tag>>(2*next_top))&3)!=3) abort(); CLEAR_X87_FLAG(X87_C1); st_top=next_top; cpu->_st_tag &= ~(3u<<(2*st_top)); }
#define POP_REGS { cpu->_st_tag |= 3u<<(2*st_top); st_top = (st_top + 1) & 7; CLEAR_X87_FLAG(X87_C1); }
#define POP2_REGS { POP_REGS; POP_REGS; }



/* HaloPad: honour the x87 precision-control field (control word bits 8-9) for the
   operations Intel defines it for (add, subtract, multiply, divide in all forms, and
   square root). Halo runs in single precision (Direct3D 9 default; Halo itself loads
   0x007E when it asks Direct3D to preserve the FPU state). The stack is modelled as
   double, so the 53-bit setting is exact and the 64-bit setting is approximated by
   53 bits (recorded limitation). */
static inline double x87_pc_round(_cpu *cpu, double v)
{
    return (((cpu->_st_cw >> 8) & 3) == 0) ? (double)(float)v : v;
}

// fpu instructions

EXTERNC void CCALL x87_fabs_void(CPU)
{
    ST0 = fabs(ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fadd_float(CPU, float_int num)
{
    ST0 += num.f;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fadd_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int num;

    num.low = num_low;
    num.high = num_high;
    ST0 += num.d;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fadd_st(CPU, int num)
{
    ST0 += ST(num);
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fadd_to_st(CPU, int num)
{
    ST(num) += ST0;
    ST(num) = x87_pc_round(cpu, ST(num));
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_faddp_st(CPU, int num)
{
    ST(num) += ST0;
    ST(num) = x87_pc_round(cpu, ST(num));
    POP_REGS;
}

EXTERNC void CCALL x87_fchs_void(CPU)
{
    ST0 = -ST0;
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fcom_float(CPU, float_int num)
{
    X87_CMP(ST0, num.f)
}

EXTERNC void CCALL x87_fcom_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int num;

    num.low = num_low;
    num.high = num_high;
    X87_CMP(ST0, num.d)
}

EXTERNC void CCALL x87_fcomp_float(CPU, float_int num)
{
    X87_CMP(ST0, num.f)
    POP_REGS;
}

EXTERNC void CCALL x87_fcomp_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int num;

    num.low = num_low;
    num.high = num_high;
    X87_CMP(ST0, num.d)
    POP_REGS;
}

EXTERNC void CCALL x87_fcos_void(CPU)
{
    ST0 = cos(ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fdiv_float(CPU, float_int num)
{
    ST0 /= num.f;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fdiv_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int num;

    num.low = num_low;
    num.high = num_high;
    ST0 /= num.d;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fdiv_st(CPU, int num)
{
    ST0 /= ST(num);
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fdiv_to_st(CPU, int num)
{
    ST(num) /= ST0;
    ST(num) = x87_pc_round(cpu, ST(num));
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fdivp_st(CPU, int num)
{
    ST(num) /= ST0;
    ST(num) = x87_pc_round(cpu, ST(num));
    POP_REGS;
}

EXTERNC void CCALL x87_fdivr_float(CPU, float_int num)
{
    ST0 = num.f / ST0;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fdivr_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int num;

    num.low = num_low;
    num.high = num_high;
    ST0 = num.d / ST0;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fdivr_st(CPU, int num)
{
    ST0 = ST(num) / ST0;
    ST0 = x87_pc_round(cpu, ST0);
}

EXTERNC void CCALL x87_fdivrp_st(CPU, int num)
{
    ST(num) = ST0 / ST(num);
    ST(num) = x87_pc_round(cpu, ST(num));
    POP_REGS;
}

EXTERNC void CCALL x87_fiadd_int32(CPU, int32_t num)
{
    ST0 += num;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fidiv_int32(CPU, int32_t num)
{
    ST0 /= num;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fidivr_int32(CPU, int32_t num)
{
    ST0 = num / ST0;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fimul_int32(CPU, int32_t num)
{
    ST0 *= num;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fild_int32(CPU, int32_t num)
{
    PUSH_REGS;
    ST0 = num;
}

EXTERNC void CCALL x87_fild_int64(CPU, uint32_t num_low, uint32_t num_high)
{
    int_int num;

    num.low = num_low;
    num.high = num_high;
    PUSH_REGS;
    ST0 = (double)num.i;
}

EXTERNC void CCALL x87_fninit_void(CPU)
{
    st_top = 0;
    st_sw_cond = 0;
    st_cw = 0x037f;
    cpu->_st_tag=0xffff;cpu->_st_mmx_active=0;
    cpu->_st_extended=0;
}

EXTERNC int32_t CCALL x87_fist_int32(CPU)
{
    double dval, orig;
    int32_t ival;

    switch ((st_cw >> X87_RC_SHIFT) & 3)
    {
    case 0: // Round to nearest (even)
        orig = ST0;
        dval = floor(orig);
        if (orig - dval > 0.5)
        {
            dval += 1.0;
        }
        else if (!(orig - dval < 0.5))
        {
            CLEAR_X87_FLAGS;
            if ((dval < 2147483648.0) && (dval > -2147483648.0))
            {
                ival = (int32_t) dval;
                ival += ival & 1;
            }
            else
            {
                ival = (int32_t) 0x80000000;
            }
            return ival;
        }
        break;
    case 1: // Round down (toward -infinity)
        dval = floor(ST0);
        break;
    case 2: // Round up (toward +infinity)
        dval = ceil(ST0);
        break;
    case 3: // Round toward zero (Truncate)
        dval = trunc(ST0);
        break;
    }
    CLEAR_X87_FLAGS;
    return ((dval < 2147483648.0) && (dval > -2147483648.0))?((int32_t) dval):((int32_t) 0x80000000);
}

EXTERNC int16_t CCALL x87_fistp_int16(CPU)
{
    double dval, orig;
    int16_t ival;

    switch ((st_cw >> X87_RC_SHIFT) & 3)
    {
    case 0: // Round to nearest (even)
        orig = ST0;
        dval = floor(orig);
        if (orig - dval > 0.5)
        {
            dval += 1.0;
        }
        else if (!(orig - dval < 0.5))
        {
            POP_REGS;
            if ((dval < 32768.0) && (dval > -32768.0))
            {
                ival = (int16_t) dval;
                ival += ival & 1;
            }
            else
            {
                ival = (int16_t) 0x8000;
            }
            return ival;
        }
        break;
    case 1: // Round down (toward -infinity)
        dval = floor(ST0);
        break;
    case 2: // Round up (toward +infinity)
        dval = ceil(ST0);
        break;
    case 3: // Round toward zero (Truncate)
        dval = trunc(ST0);
        break;
    }
    POP_REGS;
    return ((dval < 32768.0) && (dval > -32768.0))?((int16_t) dval):((int16_t) 0x8000);
}

EXTERNC int32_t CCALL x87_fistp_int32(CPU)
{
    double dval, orig;
    int32_t ival;

    switch ((st_cw >> X87_RC_SHIFT) & 3)
    {
    case 0: // Round to nearest (even)
        orig = ST0;
        dval = floor(orig);
        if (orig - dval > 0.5)
        {
            dval += 1.0;
        }
        else if (!(orig - dval < 0.5))
        {
            POP_REGS;
            if ((dval < 2147483648.0) && (dval > -2147483648.0))
            {
                ival = (int32_t) dval;
                ival += ival & 1;
            }
            else
            {
                ival = (int32_t) 0x80000000;
            }
            return ival;
        }
        break;
    case 1: // Round down (toward -infinity)
        dval = floor(ST0);
        break;
    case 2: // Round up (toward +infinity)
        dval = ceil(ST0);
        break;
    case 3: // Round toward zero (Truncate)
        dval = trunc(ST0);
        break;
    }
    POP_REGS;
    return ((dval < 2147483648.0) && (dval > -2147483648.0))?((int32_t) dval):((int32_t) 0x80000000);
}

EXTERNC uint32_t CCALL x87_fistp_int64(CPU)
{
    double orig, dval;
#ifdef BIG_ENDIAN_BYTE_ORDER
    int_int uval;
    le_int *presult;
    #define ival uval.i
#else
    int64_t ival;
#endif

    orig = ST0;
    POP_REGS;

    if ((orig >= 9223372036854775808.0) || (orig <= -9223372036854775808.0))
    {
#ifdef BIG_ENDIAN_BYTE_ORDER
        uval.i = INT64_C(0x8000000000000000);

        presult = (le_int *)&(st_result);

        presult->low = uval.low;
        presult->high = uval.high;

        return X87_RESULT();
#else
        st_result = INT64_C(0x8000000000000000);
        return X87_RESULT();
#endif
    }

    switch ((st_cw >> X87_RC_SHIFT) & 3)
    {
    case 0: // Round to nearest (even)
        dval = floor(orig);
        ival = (int64_t) dval;
        if (orig - dval > 0.5)
        {
            ival++;
        }
        else if (!(orig - dval < 0.5))
        {
            ival += ival & 1;
        }
        break;
    case 1: // Round down (toward -infinity)
        ival = (int64_t) floor(orig);
        break;
    case 2: // Round up (toward +infinity)
        ival = (int64_t) ceil(orig);
        break;
    case 3: // Round toward zero (Truncate)
        ival = (int64_t) trunc(orig);
        break;
    }

#ifdef BIG_ENDIAN_BYTE_ORDER
    #undef ival

    presult = (le_int *)&(st_result);

    presult->low = uval.low;
    presult->high = uval.high;

    return X87_RESULT();
#else
    st_result = ival;
    return X87_RESULT();
#endif
}

EXTERNC void CCALL x87_fisub_int32(CPU, int32_t num)
{
    ST0 -= num;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fisubr_int32(CPU, int32_t num)
{
    ST0 = num - ST0;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fld_float(CPU, float_int num)
{
    PUSH_REGS;
    ST0 = num.f;
}

EXTERNC void CCALL x87_fld_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int *pst0;

    PUSH_REGS;
    pst0 = (double_int *) &(ST0);
    pst0->low = num_low;
    pst0->high = num_high;
}

EXTERNC void CCALL x87_fld_st(CPU, int num)
{
    double newval;

    newval = ST(num);
    PUSH_REGS;
    ST0 = newval;
}

EXTERNC void CCALL x87_fld1_void(CPU)
{
    PUSH_REGS;
    ST0 = const_1_0;
}

EXTERNC void CCALL x87_fldlg2_void(CPU)
{
    PUSH_REGS;
    ST0 = const_lg2;
}

EXTERNC void CCALL x87_fldln2_void(CPU)
{
    PUSH_REGS;
    ST0 = const_ln2;
}

EXTERNC void CCALL x87_fldz_void(CPU)
{
    PUSH_REGS;
    ST0 = const_0_0;
}

EXTERNC void CCALL x87_fldcw_uint16(CPU, uint32_t new_cw)
{
    st_cw = new_cw & 0xffff;
}

EXTERNC void CCALL x87_fmul_float(CPU, float_int num)
{
    ST0 *= num.f;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fmul_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int num;

    num.low = num_low;
    num.high = num_high;
    ST0 *= num.d;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fmul_st(CPU, int num)
{
    ST0 *= ST(num);
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fmul_to_st(CPU, int num)
{
    ST(num) *= ST0;
    ST(num) = x87_pc_round(cpu, ST(num));
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fmulp_st(CPU, int num)
{
    ST(num) *= ST0;
    ST(num) = x87_pc_round(cpu, ST(num));
    POP_REGS;
}

EXTERNC void CCALL x87_fptan_void(CPU)
{
    ST0 = tan(ST0);
    PUSH_REGS;
    ST0 = const_1_0;
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_frstor_void(CPU, const uint8_t *state)
{
    uint32_t sw;
    int index;
    double_int value;
    int_int mantissa;
    uint32_t exponent, sign;

    st_cw = *(uint32_t *)(state) & 0xffff;
    sw = *(uint32_t *)(state + 4);
    st_top = (sw >> 11) & 7;
    st_sw_cond = sw & ~(7u << 11);
    cpu->_st_tag = *(const uint32_t *)(state+8)&0xffff;
    cpu->_st_mmx_active=0;

    for (index = 0; index <= 7; index++)
    {
        mantissa.low = *(uint32_t *)(state + 28 + index * 10);
        mantissa.high = *(uint32_t *)(state + 32 + index * 10);
        exponent = *(uint16_t *)(state + 36 + index * 10);

        mantissa.i = (mantissa.i >> 11) & UINT64_C(0x000fffffffffffff);
        sign = (exponent & 0x8000) << 16;
        exponent = (exponent & 0x7fff) + 1023 - 16383; // adjust bias
        if (exponent >= 2048) exponent = 2047;
        else if (exponent < 0) exponent = 0;

        value.low = mantissa.low;
        value.high = mantissa.high | (exponent << 20) | sign;

        ST(index) = value.d;
    }
}

EXTERNC void CCALL x87_fnsave_void(CPU, uint8_t *state)
{
    int index;
    double_int value;
    int_int mantissa;
    uint32_t exponent, sign;

    *(uint32_t *)(state) = st_cw;
    *(uint32_t *)(state + 4) = st_sw_cond | (st_top << 11);
    *(uint32_t *)(state + 8) = cpu->_st_tag;

    for (index = 0; index <= 7; index++)
    {
        value.d = ST(index);

        mantissa.low = value.low;
        mantissa.high = value.high;

        mantissa.i = (mantissa.i & UINT64_C(0x000fffffffffffff)) << 11;
        exponent = (value.high >> 20) & 0x07ff;
        sign = (value.high >> 16) & 0x8000;

        if (value.d != 0)
        {
            mantissa.i |= UINT64_C(0x8000000000000000);
            exponent = exponent + 16383 - 1023; // adjust bias
        }

        *(uint32_t *)(state + 28 + index * 10) = mantissa.low;
        *(uint32_t *)(state + 32 + index * 10) = mantissa.high;
        *(uint16_t *)(state + 36 + index * 10) = exponent | sign;
    }

    st_top = 0;
    st_sw_cond = 0;
    st_cw = 0x037f;
    cpu->_st_tag=0xffff;cpu->_st_mmx_active=0;
}

EXTERNC void CCALL x87_fsin_void(CPU)
{
    ST0 = sin(ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fsqrt_void(CPU)
{
    ST0 = sqrt(ST0);
    ST0 = x87_pc_round(cpu, ST0);
}

EXTERNC int32_t CCALL x87_fst_float(CPU)
{
    float_int ret;

    CLEAR_X87_FLAGS;
    ret.f = (float)ST0;

    return ret.i;
}

EXTERNC uint32_t CCALL x87_fst_double(CPU)
{
#ifdef BIG_ENDIAN_FLOAT_WORD_ORDER
    double_int *pst0;
    le_int *presult;

    CLEAR_X87_FLAGS;
    pst0 = (double_int *)&(ST0);
    presult = (le_int *)&(st_result);

    presult->low = pst0->low;
    presult->high = pst0->high;

    return X87_RESULT();
#else
    void *presult;

    CLEAR_X87_FLAGS;
    presult = &(ST0);

    return halopad_x87_result(cpu, (const int64_t *)presult);   /* ST0's bits, not st_result */
#endif
}

EXTERNC void CCALL x87_fst_st(CPU, int num)
{
    CLEAR_X87_FLAGS;
    ST(num) = ST0;
}

EXTERNC int32_t CCALL x87_fstp_float(CPU)
{
    float_int ret;

    ret.f = (float)ST0;
    POP_REGS;
    return ret.i;
}

EXTERNC uint32_t CCALL x87_fstp_double(CPU)
{
#ifdef BIG_ENDIAN_FLOAT_WORD_ORDER
    double_int *pst0;
    le_int *presult;

    pst0 = (double_int *)&(ST0);
    presult = (le_int *)&(st_result);

    presult->low = pst0->low;
    presult->high = pst0->high;

    POP_REGS;
    return X87_RESULT();
#else
    void *presult;

    presult = &(ST0);
    POP_REGS;
    return halopad_x87_result(cpu, (const int64_t *)presult);   /* copied before a later push reuses the register */
#endif
}

EXTERNC void CCALL x87_fstp_st(CPU, int num)
{
    ST(num) = ST0;
    POP_REGS;
}

EXTERNC uint32_t CCALL x87_fnstcw_void(CPU)
{
    return st_cw;
}

EXTERNC uint32_t CCALL x87_fnstsw_void(CPU)
{
    return st_sw_cond | (st_top << 11);
}

EXTERNC void CCALL x87_fsub_float(CPU, float_int num)
{
    ST0 -= num.f;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fsub_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int num;

    num.low = num_low;
    num.high = num_high;
    ST0 -= num.d;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fsub_st(CPU, int num)
{
    ST0 -= ST(num);
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fsub_to_st(CPU, int num)
{
    ST(num) -= ST0;
    ST(num) = x87_pc_round(cpu, ST(num));
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fsubp_st(CPU, int num)
{
    ST(num) -= ST0;
    ST(num) = x87_pc_round(cpu, ST(num));
    POP_REGS;
}

EXTERNC void CCALL x87_fsubr_float(CPU, float_int num)
{
    ST0 = num.f - ST0;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fsubr_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int num;

    num.low = num_low;
    num.high = num_high;
    ST0 = num.d - ST0;
    ST0 = x87_pc_round(cpu, ST0);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fsubr_st(CPU, int num)
{
    ST0 = ST(num) - ST0;
    ST0 = x87_pc_round(cpu, ST0);
}

EXTERNC void CCALL x87_fsubrp_st(CPU, int num)
{
    ST(num) = ST0 - ST(num);
    ST(num) = x87_pc_round(cpu, ST(num));
    POP_REGS;
}

EXTERNC void CCALL x87_fucom_st(CPU, int num)
{
    X87_CMP(ST0, ST(num))
}

EXTERNC void CCALL x87_fucomp_st(CPU, int num)
{
    X87_CMP(ST0, ST(num))
    POP_REGS;
}

EXTERNC void CCALL x87_fucompp_void(CPU)
{
    X87_CMP(ST0, ST1)
    POP2_REGS;
}

EXTERNC void CCALL x87_fxch_st(CPU, int num)
{
    double tmpst;

    tmpst = ST0;
    ST0 = ST(num);
    ST(num) = tmpst;
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fyl2x_void(CPU)
{
    ST1 *= log2(ST0);
    POP_REGS;
}


// math functions

// double acos(double x);
// x = ST0
// return value = ST0
EXTERNC void CCALL x87_facos_void(CPU)
{
    ST0 = acos(ST0);
}

// double asin(double x);
// x = ST0
// return value = ST0
EXTERNC void CCALL x87_fasin_void(CPU)
{
    ST0 = asin(ST0);
}

// double atan2(double y, double x);
//  - arc tangent of y/x
// y = ST0
// x = ST1
// return value = ST0
EXTERNC void CCALL x87_fatan2_void(CPU)
{
    ST1 = atan2(ST0, ST1);
    POP_REGS;
}

// double atan2(double y, double x);
//  - arc tangent of y/x
// y = ST1
// x = ST0
// return value = ST0
EXTERNC void CCALL x87_fatan2r_void(CPU)
{
    ST1 = atan2(ST1, ST0);
    POP_REGS;
}

// double log(double x);
// x = ST0
// return value = ST0
EXTERNC void CCALL x87_flog_void(CPU)
{
    ST0 = log(ST0);
}

// double log10(double x);
// x = ST0
// return value = ST0
EXTERNC void CCALL x87_flog10_void(CPU)
{
    ST0 = log10(ST0);
}

// double floor(double x);
// x = stack
// return value = ST0
EXTERNC void CCALL x87_floor_double(CPU, uint32_t num_low, uint32_t num_high)
{
    double_int num;

    PUSH_REGS;
    num.low = num_low;
    num.high = num_high;
    ST0 = floor(num.d);
}

// double fmod(double x, double y);
//  - the  floating-point remainder of dividing x by y
// x = ST0
// y = ST1
// return value = ST0
EXTERNC void CCALL x87_fmod_void(CPU)
{
    ST1 = fmod(ST0, ST1);
    POP_REGS;
}

// double fmod(double x, double y);
//  - the  floating-point remainder of dividing x by y
// x = ST1
// y = ST0
// return value = ST0
EXTERNC void CCALL x87_fmodr_void(CPU)
{
    ST1 = fmod(ST1, ST0);
    POP_REGS;
}

// double pow(double x, double y);
//  - the value of x raised to the power of y
// x = ST0
// y = ST1
// return value = ST0
EXTERNC void CCALL x87_fpow_void(CPU)
{
    ST1 = pow(ST0, ST1);
    POP_REGS;
}

// double pow(double x, double y);
//  - the value of x raised to the power of y
// x = ST1
// y = ST0
// return value = ST0
EXTERNC void CCALL x87_fpowr_void(CPU)
{
    ST1 = pow(ST1, ST0);
    POP_REGS;
}

// double round(double x);
// x = ST0
// return value = ST0
EXTERNC void CCALL x87_fround_void(CPU)
{
    ST0 = round(ST0);
}

// double tan(double x);
// x = ST0
// return value = ST0
EXTERNC void CCALL x87_ftan_void(CPU)
{
    ST0 = tan(ST0);
}

// truncate toward zero
EXTERNC int32_t CCALL x87_ftol_int32(CPU)
{
#if defined(__SSE2__)
    int32_t result;

    result = _mm_cvttsd_si32(_mm_load1_pd(&(ST0)));
    POP_REGS;
    return result;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    int32_t result;

    result = (int32_t)trunc(ST0);
    POP_REGS;
    return result;
#else
    const static double doublemagic = 6755399441055744.0; // 2^52 * 1.5

    double_int result;
    double num1, num2;

    num1 = ST0;
    POP_REGS;

    if (num1 < 0)
    {
        if (num1 <= -2147483648.0) return (int32_t) 0x80000000;

        result.d = num1 + doublemagic;  // fast conversion to int,
        num2 = (double)(int32_t)result.low; // result.low contains the result (rounded up or down)

        if (num2 < num1) // compare result with original value and if the result was rounded toward negative infinity, then increase result (truncate toward 0)
        {
            result.low++;
        }

        return (int32_t)result.low;
    }
    else
    {
        if (num1 >= 2147483648.0) return (int32_t) 0x80000000;

        result.d = num1 + doublemagic;  // fast conversion to int,
        if (0 > (int32_t)result.low) return (int32_t) 0x7fffffff;
        num2 = (double)(int32_t)result.low; // result.low contains the result (rounded up or down)

        if (num2 > num1) // compare result with original value and if the result was rounded toward positive infinity, then decrease result (truncate toward 0)
        {
            result.low--;
        }

        return (int32_t)result.low;
    }
#endif
}

// truncate toward zero
EXTERNC uint32_t CCALL x87_ftol_int64(CPU)
{
    double orig;

    orig = ST0;
    POP_REGS;

#ifdef BIG_ENDIAN_BYTE_ORDER
    int_int ret;
    le_int *presult;

    ret.i = ((orig < 9223372036854775808.0) && (orig > -9223372036854775808.0))?((int64_t) trunc(orig)):INT64_C(0x8000000000000000);

    presult = (le_int *)&(st_result);

    presult->low = ret.low;
    presult->high = ret.high;

    return X87_RESULT();
#else
    st_result = ((orig < 9223372036854775808.0) && (orig > -9223372036854775808.0))?((int64_t) trunc(orig)):INT64_C(0x8000000000000000);
    return X87_RESULT();
#endif
}


/* HaloPad additions (G3): x87 instructions reached by Halo's code that SR's backend did
   not implement. Transcendentals, frndint and fscale are not affected by precision
   control on x87, so none of these round to the control word's precision. The stack is
   double: extended-range operands are out of scope here, as for the rest of the model. */

EXTERNC void CCALL x87_fldpi_void(CPU)
{
    PUSH_REGS;
    ST0 = M_PI;
}

EXTERNC void CCALL x87_fldl2e_void(CPU)
{
    PUSH_REGS;
    ST0 = M_LOG2E;
}

EXTERNC void CCALL x87_fpatan_void(CPU)
{
    ST1 = atan2(ST1, ST0);
    POP_REGS;
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_frndint_void(CPU)
{
    switch ((st_cw >> X87_RC_SHIFT) & 3)
    {
    case 0: ST0 = nearbyint(ST0); break;    /* host default mode: nearest, ties to even */
    case 1: ST0 = floor(ST0); break;
    case 2: ST0 = ceil(ST0); break;
    case 3: ST0 = trunc(ST0); break;
    }
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fscale_void(CPU)
{
    double s = trunc(ST1);
    int e = (s > 100000.0) ? 100000 : (s < -100000.0) ? -100000 : (int) s;
    ST0 = ldexp(ST0, e);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_f2xm1_void(CPU)
{
    ST0 = expm1(ST0 * M_LN2);
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fsincos_void(CPU)
{
    double v = ST0;
    if (fabs(v) >= 9223372036854775808.0)  /* |x| >= 2^63: C2 set, operand unchanged */
    {
        st_sw_cond = X87_C2;
        return;
    }
    ST0 = sin(v);
    PUSH_REGS;
    ST0 = cos(v);
    CLEAR_X87_FLAGS;
}

/* Tag word not modelled: a register is never reported empty. Values subnormal in double
   are normal in x87's extended format. */
EXTERNC void CCALL x87_fxam_void(CPU)
{
    double v = ST0;
    uint32_t c;
    switch (fpclassify(v))
    {
    case FP_NAN: c = X87_C0; break;
    case FP_INFINITE: c = X87_C2 | X87_C0; break;
    case FP_ZERO: c = X87_C3; break;
    default: c = X87_C2; break;
    }
    st_sw_cond = c | (signbit(v) ? X87_C1 : 0);
}


/* HaloPad: 80-bit extended loads and stores (fld m80, fstp m80). The stack holds doubles, so
   a load rounds the 64-bit significand to 53 bits once (round to nearest even); a store is
   exact, since every double is representable in the extended format. Zeros, denormals,
   infinities and NaNs (payload kept) are converted; the address is a guest address. */
EXTERNC void CCALL x87_fld_tword(CPU, uint32_t adr)
{
    uint8_t b[10];
    uint64_t mant;
    uint16_t se;
    int e;
    double v;

    memcpy(b, REG2PTR(adr), 10);
    memcpy(&mant, b, 8);
    se = (uint16_t)(b[8] | (b[9] << 8));
    e = se & 0x7fff;
    if (e == 0x7fff)
    {
        if ((mant << 1) == 0) v = INFINITY;
        else
        {
            uint64_t bits = 0x7ff8000000000000ull | ((mant >> 11) & 0x0007ffffffffffffull);
            memcpy(&v, &bits, 8);
        }
    }
    else if (mant == 0) v = 0.0;
    else v = ldexp((double)mant, (e ? e : 1) - 16383 - 63);   /* exponent 0: denormal, scaled as exponent 1 */
    if (se & 0x8000) v = -v;
    PUSH_REGS;
    ST0 = v;
}

EXTERNC void CCALL x87_fstp_tword(CPU, uint32_t adr)
{
    uint8_t b[10];
    double v = ST0;
    uint64_t mant, bits;
    uint16_t se;

    memcpy(&bits, &v, 8);
    se = (bits >> 63) ? 0x8000 : 0;
    if (isnan(v))
    {
        mant = 0xc000000000000000ull | ((bits & 0x0007ffffffffffffull) << 11);
        se |= 0x7fff;
    }
    else if (isinf(v))
    {
        mant = 0x8000000000000000ull;
        se |= 0x7fff;
    }
    else if (v == 0.0) mant = 0;
    else
    {
        int e;
        double m = frexp(fabs(v), &e);                  /* v = m * 2^e, m in [0.5, 1) */
        mant = (uint64_t)ldexp(m, 64);                   /* exact: 53 significant bits, bit 63 set */
        se |= (uint16_t)(e + 16382);
    }
    memcpy(b, &mant, 8);
    b[8] = (uint8_t)se;
    b[9] = (uint8_t)(se >> 8);
    memcpy(REG2PTR(adr), b, 10);
    POP_REGS;
}


/* HaloPad: fprem (the CRT's fmod, 0x5cdc1f). The partial remainder of ST0 by ST1 with a
   truncated quotient, as C's fmod computes it; with the double stack the reduction is always
   complete, so C2 is clear, and C0, C3, C1 get the quotient's low three bits (when it is an
   exact integer). An invalid operand (NaN, infinite ST0, zero ST1) gives the default NaN. */
EXTERNC void CCALL x87_fprem_void(CPU)
{
    double x = ST0, y = ST1, r, q;
    uint32_t cond = 0;

    if (isnan(x) || isnan(y) || isinf(x) || y == 0.0)
    {
        ST0 = NAN;
        CLEAR_X87_FLAGS;
        return;
    }
    r = fmod(x, y);
    q = trunc((x - r) / y);
    if (fabs(q) < 9007199254740992.0)
    {
        uint64_t qi = (uint64_t) fabs(q);
        if (qi & 1) cond |= X87_C1;
        if (qi & 2) cond |= X87_C3;
        if (qi & 4) cond |= X87_C0;
    }
    ST0 = r;
    st_sw_cond = cond;
}


/* HaloPad: fsubr/fdivr st(i), st(0): ST(i) = ST0 - ST(i) and ST0 / ST(i) (the CRT's exp and
   pow core, 0x5d7294) */
EXTERNC void CCALL x87_fsubr_to_st(CPU, int num)
{
    ST(num) = x87_pc_round(cpu, ST0 - ST(num));
    CLEAR_X87_FLAGS;
}

EXTERNC void CCALL x87_fdivr_to_st(CPU, int num)
{
    ST(num) = x87_pc_round(cpu, ST0 / ST(num));
    CLEAR_X87_FLAGS;
}
