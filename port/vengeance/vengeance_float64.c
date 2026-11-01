/* Tribes: Vengeance's x87 in binary64 (VENGEANCE_X87=double, scripts/vengeance/build_engine.py):
   llasm's own x87 runtime (port/llasm-support/llasm_float.c), as HaloPad uses, in place of
   TribesPad's extended-precision one (vengeance_float80.c). Registers hold host doubles, so
   each instruction is host arithmetic, rounded to the precision the control word sets
   (single where Direct3D sets it, double where Unreal does), without converting 80-bit
   values. The exponent range and the rare double rounding of 80-bit intermediates differ
   from real hardware; values the game stores are float or double either way. */
#if VENGEANCE_X87_DOUBLE
#include "../llasm-support/llasm_float.c"
/* New guest threads start in this runtime's register format (port/core/guest_threads.c). */
int tribes_x87_extended_default = 0;
/* Register compares, which TribesPad's lifter emits as calls (llasm's own lifter inlined them). */
EXTERNC void CCALL x87_fcom_st(CPU, int n) { X87_CMP(ST0, ST(n)) }
EXTERNC void CCALL x87_fcomp_st(CPU, int n) { X87_CMP(ST0, ST(n)) POP_REGS; }
EXTERNC void CCALL x87_fcompp_void(CPU) { X87_CMP(ST0, ST(1)) POP_REGS; POP_REGS; }
#endif
