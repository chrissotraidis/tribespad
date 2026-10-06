#ifndef TRIBESPAD_GUEST_VIRTUAL_MEMORY_H
#define TRIBESPAD_GUEST_VIRTUAL_MEMORY_H
#include <stdint.h>
/* Host-only checked dynamic arena opt-in, before its first reservation. The
   backend owns atomic native backing / logical permission / zero-fill changes.
   access and zero contain one byte per 4 KiB page in a native-aligned range. */
typedef struct {
    int (*initialize)(uint64_t base,uint32_t address,uint32_t size);
    int (*change)(uint64_t base,uint32_t address,uint32_t size,const unsigned char *access,const unsigned char *zero);
} TribesVirtualMemoryBackend;
uint32_t tribes_virtual_memory_enable_checked(uint64_t base,const TribesVirtualMemoryBackend *backend);
/* Host-only diagnostic opt-in, once per guest space. This callback supplies
 * nonexecuting storage/metadata for captured patches, never native code. */
typedef uint32_t (*TribesPatchCaptureAllocator)(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_virtual_memory_enable_patch_capture(uint64_t base,TribesPatchCaptureAllocator allocator);
uint32_t tribes_virtual_alloc2_from_app(uint64_t base,uint32_t process,uint32_t address,uint32_t size,uint32_t type,uint32_t protection,uint32_t parameters,uint32_t count);
uint32_t tribes_nt_allocate_virtual_memory_ex(uint64_t base,uint32_t process,uint32_t address_pointer,uint32_t size_pointer,uint32_t type,uint32_t protection,uint32_t parameters,uint32_t count);
uint32_t tribes_virtual_alloc(uint64_t base,uint32_t address,uint32_t size,uint32_t type,uint32_t protection);
uint32_t tribes_virtual_free(uint64_t base,uint32_t address,uint32_t size,uint32_t type);
/* Owned-region inspection, under lock; visitor must not reenter this service.
   Protection zero means reserved/uncommitted; other values are Win32 PAGE_*.
   Reported runs cover the logical allocation, excluding native tail padding. */
int tribes_virtual_memory_visit(uint64_t base,int (*visitor)(uint32_t address,uint32_t size,uint32_t protection,void *context),void *context);
/* Seven DWORD x86 MEMORY_BASIC_INFORMATION fields for a page in VM arena. */
int tribes_virtual_memory_query(uint64_t base,uint32_t address,uint32_t fields[7]);
#endif
