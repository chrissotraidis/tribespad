#ifndef TRIBESPAD_GUEST_MODULES_H
#define TRIBESPAD_GUEST_MODULES_H
#include <stdint.h>
int tribes_module_set_pending(uint64_t base,uint32_t image,int pending);
uint32_t tribes_module_pending_image(uint64_t base,const char *name);
/* Metadata registration for images already mapped/linked by the host runtime.
   This does not load images, apply relocations, initialize DLLs or resolve exports. */
int tribes_module_register(uint64_t base,uint32_t image,const char *path,int main_image);
/* SizeOfImage from the host's audited PE manifest; rejects overlapping ranges.
   The loader retains one reference for the linked image. Guest added references
   are balanced; trying to unload that last loader reference stops explicitly. */
int tribes_module_register_image(uint64_t base,uint32_t image,uint32_t size,const char *path,int main_image);
/* Host-audited complete numeric resource catalog, one language per type/name.
   RVAs refer to the already mapped image; no guest PE headers are trusted. */
typedef struct {uint32_t type,name,language,entry_rva,data_rva,size;} TribesImageResource;
int tribes_module_register_resources(uint64_t base,uint32_t image,const TribesImageResource *resources,uint32_t count);
uint32_t tribes_find_resource_a(uint64_t base,uint32_t image,uint32_t name,uint32_t type);
uint32_t tribes_sizeof_resource(uint64_t base,uint32_t image,uint32_t resource);
uint32_t tribes_load_resource(uint64_t base,uint32_t image,uint32_t resource);
uint32_t tribes_lock_resource(uint64_t base,uint32_t resource);
uint32_t tribes_get_module_handle_ex_w(uint64_t base,uint32_t flags,uint32_t name,uint32_t output);
uint32_t tribes_get_module_handle_ex_a(uint64_t base,uint32_t flags,uint32_t name,uint32_t output);
typedef struct { const char *name;uint16_t ordinal;uint32_t address; } TribesNativeExport;
/* Native providers have precompiled exports and load references, no guest DllMain. */
int tribes_module_define_native(uint64_t base,uint32_t image,const char *path,const TribesNativeExport *exports,uint32_t count);
/* Add one host-loader reference to an already-defined native provider. */
int tribes_module_retain_native(uint64_t base,uint32_t image);
/* Immutable host-audited bare-name absence. No image or exports are published.
   Conflicts with registered/defined providers are rejected, even if unloaded.
   Relative bare-name loads return 0/error 126; unclassified names and explicit
   paths still stop. Namespace discovery/PE loading are host responsibilities. */
int tribes_module_define_absent(uint64_t base,const char *name);
uint32_t tribes_load_library(uint64_t base,uint32_t name);
uint32_t tribes_load_library_ex_a(uint64_t base,uint32_t name,uint32_t file,uint32_t flags);
uint32_t tribes_load_library_ex_w(uint64_t base,uint32_t name,uint32_t file,uint32_t flags);
uint32_t tribes_free_library(uint64_t base,uint32_t image);
uint32_t tribes_get_proc_address(uint64_t base,uint32_t image,uint32_t name);
uint32_t tribes_get_module_handle(uint64_t base,uint32_t name);
uint32_t tribes_get_module_handle_w(uint64_t base,uint32_t name);
uint32_t tribes_get_module_filename(uint64_t base,uint32_t module,uint32_t output,uint32_t capacity);
uint32_t tribes_get_module_filename_w(uint64_t base,uint32_t module,uint32_t output,uint32_t capacity);
#endif
