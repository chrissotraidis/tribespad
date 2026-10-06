/* Finite provider identity, export ownership and ARM64 wrapper routing. */
#include "native_system_providers.h"
#include "guest_modules.h"
#include "native_system_providers.inc"
int tribes_system_providers_configure(uint64_t base) {
    for(unsigned i=0;i<sizeof system_providers/sizeof system_providers[0];++i){
        const SystemProvider *p=&system_providers[i];
        if(!tribes_module_define_native(base,p->image,p->path,p->exports,p->count))return 0;
        for(uint32_t r=0;r<p->initial_references;r++)if(!tribes_module_retain_native(base,p->image))return 0;
    }
    return 1;
}
void *tribes_system_native_lookup(uint32_t address) {
    for(unsigned i=0;i<sizeof system_routes/sizeof system_routes[0];++i)
        if(address==system_routes[i].guest_va)return system_routes[i].host_function;
    return 0;
}
