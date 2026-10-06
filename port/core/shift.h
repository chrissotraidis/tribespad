#ifndef TRIBESPAD_SHIFT_H
#define TRIBESPAD_SHIFT_H
#include <stdint.h>
enum TBShiftOp { TB_SHL, TB_SHR, TB_SAR, TB_ROL, TB_SHRD, TB_SHLD, TB_ROR };
typedef struct { uint32_t value, flags; } TBShiftResult;
TBShiftResult tb_shift(unsigned op,unsigned width,uint32_t value,uint32_t source,unsigned count,uint32_t flags);
#endif
