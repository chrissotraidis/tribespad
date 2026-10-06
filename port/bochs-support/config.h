/* Standalone native math configuration; excludes the Bochs CPU interpreter. */
#ifndef TRIBESPAD_BOCHS_MATH_CONFIG_H
#define TRIBESPAD_BOCHS_MATH_CONFIG_H
#include <stdint.h>
#define BX_LITTLE_ENDIAN 1
#define BX_CPP_INLINE inline
#define BX_CONST64(x) UINT64_C(x)
#define SOFTFLOAT_FAST_DIV32TO16 1
#define SOFTFLOAT_FAST_DIV64TO32 1
typedef int16_t Bit16s;
typedef uint16_t Bit16u;
typedef int32_t Bit32s;
typedef uint32_t Bit32u;
typedef int64_t Bit64s;
typedef uint64_t Bit64u;
#endif
