/* Registration part of Win32 SetUnhandledExceptionFilter.
   Stored values are original guest PCs. Fault-to-guest SEH dispatch remains open.
   Contract: https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter */
#include <stdatomic.h>
#include <stdint.h>
static _Atomic(uint32_t) top_level_filter;
uint32_t tribes_set_exception_filter(uint32_t guest_pc) {
    return atomic_exchange_explicit(&top_level_filter,guest_pc,memory_order_seq_cst);
}
uint32_t tribes_exception_filter(void) {
    return atomic_load_explicit(&top_level_filter,memory_order_seq_cst);
}

/* Process error-mode registration for one guest process per runtime.
   Native services already return failures without Windows dialogs. Windows
   Error Reporting and fault-to-guest SEH delivery are not implemented here.
   https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode */
static _Atomic(uint32_t) process_error_mode;
uint32_t tribes_get_error_mode(void) {
    return atomic_load_explicit(&process_error_mode,memory_order_seq_cst);
}
uint32_t tribes_set_error_mode(uint32_t mode) {
    uint32_t previous=atomic_load_explicit(&process_error_mode,memory_order_seq_cst);
    /* SEM_NOALIGNMENTFAULTEXCEPT cannot be cleared once requested. */
    while(!atomic_compare_exchange_weak_explicit(&process_error_mode,&previous,
          mode|(previous&4u),memory_order_seq_cst,memory_order_seq_cst)){}
    return previous;
}
