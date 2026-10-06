#ifndef TRIBESPAD_GUEST_THREADS_H
#define TRIBESPAD_GUEST_THREADS_H
#include "guest_callback.h"
typedef struct {
    uint32_t image,entry,tls_index,template_size,zero_fill,callback_count;
    const unsigned char *template_bytes;
    const uint32_t *callbacks;
    uint32_t dynamic; /* Fixed, mapped AOT image; no static TLS; attach on LoadLibrary. */
} TribesThreadModule; /* tls_index UINT32_MAX means no static TLS. */
/* Host metadata is copied once before process attach. Modules are listed in
   initialization order; their original TLS/DllMain callbacks run serialized. */
int tribes_threads_configure(uint64_t base,uint32_t pid,uint32_t main_tid,uint32_t stack_reserve,const TribesThreadModule *modules,uint32_t count);
void tribes_loader_lock(void);
void tribes_loader_unlock(void);
uint32_t tribes_threads_load_image(_cpu *cpu,uint32_t image);
uint32_t tribes_disable_thread_library_calls(uint64_t base,uint32_t image);
void tribes_threads_process_ready(uint64_t base);
uint32_t tribes_create_thread(uint64_t base,uint32_t attributes,uint32_t stack_size,uint32_t start,uint32_t parameter,uint32_t flags,uint32_t id_output);
uint32_t tribes_queue_user_work_item(uint64_t base,uint32_t start,uint32_t parameter,uint32_t flags);
_Noreturn void tribes_exit_thread(_cpu *cpu,uint32_t code);
_Noreturn void tribes_free_library_and_exit_thread(_cpu *cpu,uint32_t module,uint32_t code);
uint32_t tribes_guest_threads_live_count(void);
#endif
