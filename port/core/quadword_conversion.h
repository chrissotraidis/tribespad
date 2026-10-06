#ifndef TRIBESPAD_QUADWORD_CONVERSION_H
#define TRIBESPAD_QUADWORD_CONVERSION_H
#include <stdint.h>
enum TBQuadConversion {TB_QC_PS_U64,TB_QC_PS_I64,TB_QC_PD_U64,TB_QC_PD_I64,
                       TB_QC_U64_PD,TB_QC_I64_PD,TB_QC_I64_PS};
/* One-lane result and newly raised MXCSR flags. No register publication,
   sticky merge, packed exception priority or Windows fault delivery here.
   Floating input truncates; integer input uses MXCSR rounding control.
   PS input ignores upper32 bits; PS output is zero-extended. */
int tb_quadword_conversion(unsigned operation,uint64_t input,uint32_t control,
                           uint64_t *result,uint32_t *raised);
#endif
