#ifndef TRIBESPAD_GUEST_CALLBACK_H
#define TRIBESPAD_GUEST_CALLBACK_H
#include "../llasm-support/llasm_cpu.h"
/* Reenter finite AOT dispatch using a copied register context, shared thread FP
   state and caller-supplied stack top below live host-service scratch data. */
uint32_t tribes_call_guest(_cpu *parent,uint32_t target,uint32_t count,const uint32_t *arguments,uint32_t stack_top);
/* Cdecl callbacks pop only the return address; this host frame owns argument
   cleanup. Arguments remain in shared guest stack memory after the call. */
uint32_t tribes_call_guest_cdecl(_cpu *parent,uint32_t target,uint32_t count,const uint32_t *arguments,uint32_t stack_top);
/* Current copied CPU for diagnostics on the calling native thread. A nonlocal
   guest exit must clear the scope before discarding its native stack frames. */
_cpu *tribes_guest_callback_context(void);
void tribes_guest_callback_reset(void);
/* Trusted native finalizers run on the exiting guest thread while its CPU,
   stack and TEB are still valid. Registration is idempotent per native thread. */
typedef void (*TribesGuestFinalizer)(_cpu *cpu);
int tribes_guest_finalizer_register(TribesGuestFinalizer function);
void tribes_guest_thread_finalize(_cpu *cpu);
#endif
