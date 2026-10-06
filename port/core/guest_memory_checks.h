#ifndef TRIBESPAD_GUEST_MEMORY_CHECKS_H
#define TRIBESPAD_GUEST_MEMORY_CHECKS_H
#include <stdint.h>
/* Trusted host setup, once before guest threads exist. The default permission
   mask is RW; native mappings still enforce residency. Software restrictions
   are an additional layer. Enabling this does NOT attest executable coverage. */
int tribes_memory_checks_configure(uint64_t base,uint64_t size);
/* 4 KiB aligned bounds, bits READ=1/WRITE=2. Serialized against bracketed
   accesses; no concurrent instruction may straddle a permission publication. */
int tribes_memory_permissions_set(uint32_t address,uint64_t size,uint32_t access);
/* Nested access brackets retain one outer read lock per native thread.
   Permission writers cannot run until every nested bracket has ended. */
uint32_t tribes_memory_access_begin(const void *pointer,uint64_t size,uint32_t access);
uint32_t tribes_memory_transfer_begin(void *destination,const void *source,uint64_t size);
void tribes_memory_access_end(uint32_t token);
/* Trusted loader/VirtualProtect transactions. Callers must have complete
   instrumented access coverage and external call gates before guest execution.
   Static initialization also denies data access above the guest user limit. */
int tribes_memory_initialize_static(uint64_t base,const unsigned char *access,uint64_t pages);
int tribes_memory_protect_pages(uint64_t base,uint32_t address,uint32_t size,const unsigned char *access);
/* Dynamic VM arena: one-time empty-arena initialization followed by atomic
   4 KiB permission/zero-fill transactions over complete native page groups. */
int tribes_memory_vm_initialize(uint64_t base,uint32_t address,uint32_t size);
int tribes_memory_vm_change(uint64_t base,uint32_t address,uint32_t size,const unsigned char *access,const unsigned char *zero);
/* One-time AOT code publication. Compare every selected guest byte against
   host-owned expected bytes under the access lock, then forbid later writes
   to these exact intervals independently of logical page protections. The
   caller must already have linked all corresponding finite native routes. */
typedef struct { uint32_t address,size; const unsigned char *expected; } TribesPublishedCodeRange;
int tribes_memory_publish_code(uint64_t base,const TribesPublishedCodeRange *ranges,uint32_t count);
/* One captured 34-trampoline bank addition, owner-only at completed revision20. */
int tribes_memory_publish_pending_bank(uint64_t base,const TribesPublishedCodeRange *ranges,uint32_t count);
/* Captured 26-trampoline renderer group, owner-only at completed revision30. */
int tribes_memory_publish_renderer_bank(uint64_t base,const TribesPublishedCodeRange *ranges,uint32_t count);
int tribes_memory_publish_audio_bank(uint64_t base,const TribesPublishedCodeRange *ranges,uint32_t count);
int tribes_memory_code_published(uint64_t base);
/* Thirty-three finite, single-owner startup revisions. Trusted setup copies exact
   ordered writer/address/width/preimage/value records. Ordinary writes remain
   forbidden. State:0 unpublished,1 initial,3 partial,2*N completed revision N.
   Chained overlaps require consistent preimages. New plans require a completed
   preceding plan and its validated final bytes. No thirty-fourth primary revision. */
typedef struct { uint32_t pc,address,size; uint64_t before,value; } TribesCodeWrite;
int tribes_memory_prepare_code_revision(uint64_t base,const TribesCodeWrite *writes,uint32_t count);
int tribes_memory_prepare_next_code_revision(uint64_t base,const TribesCodeWrite *writes,uint32_t count);
/* Two optional, independent font-size plans at completed revision27. They
   preserve the primary phase; readiness becomes true only at checked finish. */
int tribes_memory_prepare_font_size_revision(uint64_t base,uint32_t index,const TribesCodeWrite *write);
int tribes_memory_font_size_published(uint64_t base,uint32_t index);
int tribes_memory_apply_code_write(uint64_t base,uint32_t pc,const void *pointer,uint32_t size,uint64_t value);
int tribes_memory_finish_code_revision(uint64_t base);
uint32_t tribes_memory_code_revision(uint64_t base);
/* Last fully completed phase, even during a later finite write transaction.
   A route may use this only after proving it is unaffected by that revision. */
uint32_t tribes_memory_code_completed_revision(uint64_t base);
/* Three pinned mission-time MOV immediate fields. Native readers load the
   actual guest fields; only their original MOVSS writers may update them,
   after startup publication, on its owner, with real RW permissions. */
int tribes_memory_write_mission_literal(uint64_t base,uint32_t pc,uint32_t address,uint32_t value);
#endif
