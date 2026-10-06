#ifndef TRIBESPAD_NATIVE_HANDLES_H
#define TRIBESPAD_NATIVE_HANDLES_H
#include <stdint.h>
#include <limits.h>
/* Host-owned snapshot of the named CreateFile open. Same-access duplicates
   share this immutable record; raw adopted descriptors have no such record. */
typedef struct TribesFileOpenInfo {
    uint32_t access,mode;
    char name[PATH_MAX],host[PATH_MAX];
} TribesFileOpenInfo;
/* Adopt a descriptor; ownership transfers on success. Borrow duplicates it. */
int tribes_named_mutex_configure(const char *directory);
uint32_t tribes_release_mutex(uint32_t handle);
uint32_t tribes_create_mutex(uint64_t base,uint32_t attributes,uint32_t initial_owner,uint32_t name);
uint32_t tribes_create_event(uint64_t base,uint32_t attributes,uint32_t manual,uint32_t initial,uint32_t name);
uint32_t tribes_set_event(uint32_t handle);
uint32_t tribes_reset_event(uint32_t handle);
uint32_t tribes_create_semaphore(uint64_t base,uint32_t attributes,uint32_t initial,uint32_t maximum,uint32_t name);
uint32_t tribes_release_semaphore(uint64_t base,uint32_t handle,uint32_t count,uint32_t previous);
uint32_t tribes_wait_multiple_objects(uint64_t base,uint32_t count,uint32_t handles,uint32_t all,uint32_t milliseconds);
uint32_t tribes_wait_single_object(uint32_t handle,uint32_t milliseconds);
uint32_t tribes_fd_adopt(int descriptor);
/* Internal file access: data read/write/delete share bits are 1/2/4;
   attribute read/write bits 8/16 do not participate in Windows share checks.
   SEEK is a borrow request requiring either data read or data write. */
enum { TRIBES_FILE_READ=1, TRIBES_FILE_WRITE=2, TRIBES_FILE_DELETE=4,
       TRIBES_FILE_READ_ATTRIBUTES=8, TRIBES_FILE_WRITE_ATTRIBUTES=16,
       TRIBES_FILE_SEEK=128 };
uint32_t tribes_file_fd_adopt(int descriptor,uint32_t access,uint32_t share);
uint32_t tribes_named_file_fd_adopt(int descriptor,uint32_t access,uint32_t share,const TribesFileOpenInfo *info);
uint32_t tribes_named_file_fd_adopt_truncated(int descriptor,uint32_t access,uint32_t share,const TribesFileOpenInfo *info);
int tribes_named_file_borrow_nt(uint32_t handle,uint32_t *status,TribesFileOpenInfo *info);
int tribes_fd_borrow(uint32_t handle);
/* NT file queries: owned duplicate, typed NTSTATUS errors, no LastError change.
   status points to native-owned storage, never directly to guest memory. */
int tribes_fd_borrow_nt(uint32_t handle,uint32_t *status);
int tribes_fd_borrow_access(uint32_t handle,uint32_t rights);
uint32_t tribes_close_handle(uint32_t handle);
uint32_t tribes_get_std_handle(uint32_t selector);
uint32_t tribes_set_std_handle(uint32_t selector,uint32_t handle);
uint32_t tribes_get_file_type(uint32_t handle);
uint32_t tribes_get_current_process(void);
uint32_t tribes_get_current_thread(void);
uint32_t tribes_duplicate_handle(uint64_t base,uint32_t source_process,uint32_t source,uint32_t target_process,uint32_t output,uint32_t access,uint32_t inherit,uint32_t options);
/* Native guest-thread objects: the caller receives one running-owner reference
   in addition to the returned handle. Finish signals and releases that owner. */
typedef struct TribesThreadObject TribesThreadObject;
TribesThreadObject *tribes_thread_object_create(uint32_t id,uint32_t suspended,uint32_t *handle);
void tribes_thread_object_set_current(TribesThreadObject *thread);
void tribes_thread_object_wait_start(TribesThreadObject *thread);
void tribes_thread_object_finish(TribesThreadObject *thread,uint32_t code);
uint32_t tribes_thread_object_live_count(void);
uint32_t tribes_set_thread_priority(uint32_t handle,uint32_t priority);
uint32_t tribes_resume_thread(uint32_t handle);
uint32_t tribes_get_exit_code_thread(uint64_t base,uint32_t handle,uint32_t output);
uint32_t tribes_get_thread_id(uint32_t handle);
/* Single-consumer completion queues. Socket ownership retains a private binding;
   a closed public port cannot be revived by a late native completion. */
typedef struct TribesCompletionBinding TribesCompletionBinding;
TribesCompletionBinding *tribes_completion_binding_create(uint32_t port,uint32_t key);
void tribes_completion_binding_release(TribesCompletionBinding *binding);
uint32_t tribes_completion_binding_post(TribesCompletionBinding *binding,uint32_t bytes,uint32_t overlapped);
/* Real native I/O: status/error and the checked guest status block accompany
   the packet. Public PostQueuedCompletionStatus keeps its pointer opaque. */
uint32_t tribes_completion_binding_post_io(TribesCompletionBinding *binding,uint64_t base,uint32_t bytes,uint32_t overlapped,uint32_t status,uint32_t error);
uint32_t tribes_create_io_completion_port(uint64_t base,uint32_t file,uint32_t existing,uint32_t key,uint32_t concurrency);
uint32_t tribes_post_queued_completion(uint32_t handle,uint32_t bytes,uint32_t key,uint32_t overlapped);
uint32_t tribes_get_queued_completion(uint64_t base,uint32_t handle,uint32_t bytes,uint32_t key,uint32_t overlapped,uint32_t milliseconds);
uint32_t tribes_get_queued_completion_ex(uint64_t base,uint32_t handle,uint32_t entries,uint32_t capacity,uint32_t removed,uint32_t milliseconds,uint32_t alertable);
#endif
