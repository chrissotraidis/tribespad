#ifndef TRIBESPAD_EXT80_H
#define TRIBESPAD_EXT80_H
#include "x87_status.h"
enum { TB_EXT_ADD, TB_EXT_SUB, TB_EXT_MUL, TB_EXT_DIV, TB_EXT_SQRT };
/* Pure arithmetic candidate: no stack mutation or exception delivery.
   Returns 0 for a reserved precision control or an unknown operation.
   C1 rounding direction is not modeled by this foundation. */
int tb_ext80_arithmetic(unsigned op, TBExt80 a, TBExt80 b, uint16_t control,
                        TBExt80 *result, uint16_t *exceptions);
#endif
