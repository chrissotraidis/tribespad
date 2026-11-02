/* Tribes: Vengeance's binary64 x87 runtime (vengeance_float64.c) as bitcode that
   scripts/vengeance/build_engine.py links into each translated module, so the
   compiler inlines each instruction's helper where the game's code calls it.
   The translated code passes a float operand as a 32-bit integer; the runtime
   takes it as a union, which arm64 passes as a 64-bit value, and a call whose
   type differs from its callee's is not inlined. These nine helpers keep their
   bodies under another name here, behind wrappers with the translated code's
   own signature. The executable still links vengeance_float64.c itself. */
#define x87_fadd_float x87_fadd_float_union
#define x87_fcom_float x87_fcom_float_union
#define x87_fcomp_float x87_fcomp_float_union
#define x87_fdiv_float x87_fdiv_float_union
#define x87_fdivr_float x87_fdivr_float_union
#define x87_fld_float x87_fld_float_union
#define x87_fmul_float x87_fmul_float_union
#define x87_fsub_float x87_fsub_float_union
#define x87_fsubr_float x87_fsubr_float_union
#include "vengeance_float64.c"
#undef x87_fadd_float
#undef x87_fcom_float
#undef x87_fcomp_float
#undef x87_fdiv_float
#undef x87_fdivr_float
#undef x87_fld_float
#undef x87_fmul_float
#undef x87_fsub_float
#undef x87_fsubr_float
#define VG_WRAP(name) EXTERNC void CCALL name(CPU, int32_t num) { float_int value; value.i = num; name##_union(cpu, value); }
VG_WRAP(x87_fadd_float)
VG_WRAP(x87_fcom_float)
VG_WRAP(x87_fcomp_float)
VG_WRAP(x87_fdiv_float)
VG_WRAP(x87_fdivr_float)
VG_WRAP(x87_fld_float)
VG_WRAP(x87_fmul_float)
VG_WRAP(x87_fsub_float)
VG_WRAP(x87_fsubr_float)
