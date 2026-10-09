#ifndef TRIBESPAD_SSE_ARITHMETIC_H
#define TRIBESPAD_SSE_ARITHMETIC_H
#include <stdint.h>
enum TBSSEArithmetic { TB_SSE_ADD64=0, TB_SSE_SUB64, TB_SSE_MUL32, TB_SSE_ADD32, TB_SSE_DIV32, TB_SSE_SUB32 };
/* Candidate result/status for scalar SSE/VEX, without register publication.
   Caller owns sticky MXCSR merging and unmasked Windows exception delivery.
   Returns 0 on an invalid operation/control or null output pointer.
   MUL32/ADD32/DIV32/SUB32 ignore upper input bits and zero-extends the candidate result. */
/* Native callers receive the two outputs in the value ABI, avoiding temporary
   output-pointer accesses. The pointer API below remains for existing users. */
typedef struct { uint64_t value; uint32_t raised; int valid; } TBSSEResult;
TBSSEResult tb_sse_evaluate(unsigned op,uint64_t a,uint64_t b,uint32_t mxcsr);
int tb_sse_arithmetic(unsigned op,uint64_t a,uint64_t b,uint32_t mxcsr,
                      uint64_t *result,uint32_t *raised);
/* Unary binary64 square root with the same candidate/status contract. */
int tb_sse_sqrt64(uint64_t source,uint32_t mxcsr,uint64_t *result,uint32_t *raised);
/* Unary binary32 square root; zero-extends the candidate result. */
int tb_sse_sqrt32(uint32_t source,uint32_t mxcsr,uint64_t *result,uint32_t *raised);
/* Binary64 multiplication candidate/status without register publication. */
int tb_sse_multiply64(uint64_t left,uint64_t right,uint32_t mxcsr,uint64_t *result,uint32_t *raised);
/* Binary64 division candidate/status without register publication. */
int tb_sse_divide64(uint64_t left,uint64_t right,uint32_t mxcsr,uint64_t *result,uint32_t *raised);
#endif
