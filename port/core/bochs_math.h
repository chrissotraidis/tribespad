#ifndef TRIBESPAD_BOCHS_MATH_H
#define TRIBESPAD_BOCHS_MATH_H
#include "x87_status.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Pure software math, compiled ahead of time. No CPU interpreter. */
enum { TB_BO_ROUND_INTEGER = 5 }; /* Unary arithmetic; ignores precision control. */
int tb_bo_math(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,
               TBExt80 *result,uint16_t *flags,uint64_t *quotient);
int tb_bo_arithmetic(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,
                     TBExt80 *result,uint16_t *flags);
int tb_bo_store_real(unsigned width,TBExt80 value,uint16_t control,
                     uint64_t *bits,uint16_t *flags);
int tb_bo_store_integer(unsigned width,TBExt80 value,uint16_t control,
                        int64_t *result,uint16_t *flags);
#ifdef __cplusplus
}
#endif
#endif
