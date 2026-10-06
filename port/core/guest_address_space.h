#ifndef TRIBESPAD_GUEST_ADDRESS_SPACE_H
#define TRIBESPAD_GUEST_ADDRESS_SPACE_H
#include <stdint.h>
/* Loader-owned logical x86 mappings. These declarations do not change native
   permissions; the loader must enforce its mapping before publishing metadata.
   Runtime-owned heap/provider/VM arenas are inspected through their owners. */
uint32_t tribes_address_space_begin(uint64_t base);
uint32_t tribes_address_space_declare(uint64_t base,uint32_t address,uint32_t size,uint32_t allocation,uint32_t allocation_protect,uint32_t state,uint32_t protection,uint32_t type);
uint32_t tribes_address_space_seal(uint64_t base);
/* Host-only opt-in after linking checked accesses and enforced external gates.
   Initializes the software shadow from the sealed loader-owned static map. */
typedef struct TribesCheckedMemoryBackend {
 int (*initialize)(uint64_t,const unsigned char *,uint64_t);
 int (*protect)(uint64_t,uint32_t,uint32_t,const unsigned char *);
} TribesCheckedMemoryBackend;
uint32_t tribes_address_space_enable_checked(uint64_t base,const TribesCheckedMemoryBackend *backend);
/* Runtime-owned x87 result page. Commit exactly one native data page inside
   0x2f000000..0x30000000 and publish matching checked/query metadata. */
uint32_t tribes_address_space_commit_scratch(uint64_t base,uint32_t address,uint32_t size);
/* Host-only single-threaded initialization capture. Permit one exact 64 KiB
 * allocation and logical executable protections only on specified image pages.
 * All backing remains nonexecuting; this neither compiles nor publishes code.
 * Requires checked accesses/external gates and fail-closed guest dispatch. */
uint32_t tribes_address_space_enable_patch_capture(uint64_t base,uint32_t allocation,const uint32_t (*image_ranges)[2],uint32_t count);
uint32_t tribes_address_space_protect(uint64_t base,uint32_t address,uint32_t size,uint32_t protection);
uint32_t tribes_virtual_query(uint64_t base,uint32_t address,uint32_t output,uint32_t length);
/* Exact data-page native backing. Unsupported subpage/executable/dynamic-domain
   requests fail with ERROR_NOT_SUPPORTED (50), without changing permissions. */
uint32_t tribes_virtual_protect(uint64_t base,uint32_t address,uint32_t size,uint32_t protection,uint32_t output);
#endif
