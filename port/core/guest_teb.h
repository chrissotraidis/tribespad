#ifndef TRIBESPAD_GUEST_TEB_H
#define TRIBESPAD_GUEST_TEB_H
#include <stdint.h>
/* Shared, bounded x86 process metadata. The loader owns already mapped storage
   and the immutable UTF-16/environment buffers for the process lifetime.
   This is the public process-parameters prefix plus Environment, not a loader
   list, native NT heap structure or complete Windows PEB implementation. */
typedef struct {
    uint32_t peb,parameters,image,image_path,image_path_bytes,command_line,command_line_bytes,environment;
    uint32_t handles[3];
} TribesProcessMetadata;
int tribes_teb_configure_process(uint64_t base,const TribesProcessMetadata *metadata);
/* Host configures its current guest thread using already mapped writable TEB
   storage. This does not allocate a thread, TEB pages or a TLS array. */
uint32_t tribes_teb_configure(uint64_t base,uint32_t teb,uint32_t stack_base,uint32_t stack_limit,uint32_t process_id,uint32_t thread_id,uint32_t tls_array);
void tribes_teb_clear(void);
int tribes_teb_get_ids(uint64_t base,uint32_t *pid,uint32_t *tid);
uint32_t x86_read_fs_dword(uint32_t offset);
void x86_write_fs_dword(uint32_t offset,uint32_t value);
/* x86 TEB LastStatusValue (0xbf4); separate from Win32 LastError. */
void tribes_teb_set_last_status(uint32_t status);
/* Win32 IDs match this guest thread's configured TEB identity. */
uint32_t tribes_get_current_process_id(void);
uint32_t tribes_get_current_thread_id(void);
#endif
