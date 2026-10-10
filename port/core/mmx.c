#include "mmx.h"
#include <stdlib.h>

/* Fixed-width value operations let Clang use native SIMD without host pointers
   or changing the checked guest-memory boundary. Casts preserve the MMX bits;
   convertvector performs signed widening or lane truncation. */
typedef uint8_t u8x8 __attribute__((vector_size(8)));
typedef uint16_t u16x4 __attribute__((vector_size(8)));
typedef int16_t i16x4 __attribute__((vector_size(8)));
typedef int16_t i16x8 __attribute__((vector_size(16)));
typedef uint32_t u32x2 __attribute__((vector_size(8)));
typedef int32_t i32x2 __attribute__((vector_size(8)));
typedef uint32_t u32x4 __attribute__((vector_size(16)));
typedef int32_t i32x4 __attribute__((vector_size(16)));

uint64_t tb_mmx_eval(unsigned op,uint64_t a,uint64_t b) {
 switch(op) {
 case TB_MOVQ:return b;
 case TB_PAND:return a&b;
 case TB_POR:return a|b;
 case TB_PXOR:return a^b;
 case TB_PADDB:return (uint64_t)((u8x8)a+(u8x8)b);
 case TB_PADDW:return (uint64_t)((u16x4)a+(u16x4)b);
 case TB_PSUBW:return (uint64_t)((u16x4)a-(u16x4)b);
 case TB_PMULHW:case TB_PMADDWD: {
  i32x4 p=__builtin_convertvector((i16x4)a,i32x4)*
           __builtin_convertvector((i16x4)b,i32x4);
  u32x4 bits=(u32x4)p;
  if(op==TB_PMULHW)
   return (uint64_t)__builtin_convertvector(bits>>16,u16x4);
  /* The pair -32768*-32768 + -32768*-32768 wraps to 0x80000000. */
  return (uint64_t)(__builtin_shufflevector(bits,bits,0,2)+
                    __builtin_shufflevector(bits,bits,1,3));
 }
 case TB_PUNPCKLBW:
  return (uint64_t)__builtin_shufflevector((u8x8)a,(u8x8)b,0,8,1,9,2,10,3,11);
 case TB_PUNPCKHBW:
  return (uint64_t)__builtin_shufflevector((u8x8)a,(u8x8)b,4,12,5,13,6,14,7,15);
 case TB_PUNPCKLWD:
  return (uint64_t)__builtin_shufflevector((u16x4)a,(u16x4)b,0,4,1,5);
 case TB_PUNPCKLDQ:
  return (uint64_t)__builtin_shufflevector((u32x2)a,(u32x2)b,0,2);
 case TB_PACKUSWB: {
  i16x8 v=__builtin_shufflevector((i16x4)a,(i16x4)b,0,1,2,3,4,5,6,7);
  v &= ~(v<0);
  i16x8 high=v>255;
  v=(v&~high)|(255&high);
  return (uint64_t)__builtin_convertvector(v,u8x8);
 }
 case TB_PACKSSDW: {
  i32x4 v=__builtin_shufflevector((i32x2)a,(i32x2)b,0,1,2,3);
  i32x4 low=v < -32768;
  v=(v&~low)|(-32768&low);
  i32x4 high=v>32767;
  v=(v&~high)|(32767&high);
  return (uint64_t)__builtin_convertvector(v,i16x4);
 }
 /* x86 uses the entire unsigned 64-bit count, not a masked lane count. */
 case TB_PSRAW:return (uint64_t)((i16x4)a>>(unsigned)(b<16?b:15));
 case TB_PSRLW:return b<16?(uint64_t)((u16x4)a>>(unsigned)b):0;
 case TB_PSLLW:return b<16?(uint64_t)((u16x4)a<<(unsigned)b):0;
 case TB_PSRLD:return b<32?(uint64_t)((u32x2)a>>(unsigned)b):0;
 case TB_PSLLD:return b<32?(uint64_t)((u32x2)a<<(unsigned)b):0;
 case TB_PSRLQ:return b<64?a>>b:0;
 case TB_PSLLQ:return b<64?a<<b:0;
 default:abort(); /* Never silently approximate an unsupported operation. */
 }
}
