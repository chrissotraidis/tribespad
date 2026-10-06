#ifndef TRIBESPAD_MMX_H
#define TRIBESPAD_MMX_H
#include <stdint.h>
/* Stable operation IDs shared by the SRW lowering and native runtime. */
enum TBMMXOp { TB_MOVQ=0, TB_PADDB, TB_PADDW, TB_PSUBW, TB_PAND, TB_POR,
 TB_PXOR, TB_PMULHW, TB_PMADDWD, TB_PUNPCKLBW, TB_PUNPCKHBW,
 TB_PUNPCKLWD, TB_PUNPCKLDQ, TB_PACKUSWB, TB_PACKSSDW,
 TB_PSRAW, TB_PSRLW, TB_PSLLW, TB_PSRLD, TB_PSLLD, TB_PSRLQ, TB_PSLLQ };
uint64_t tb_mmx_eval(unsigned op, uint64_t a, uint64_t b);
#endif
