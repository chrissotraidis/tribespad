#ifndef TRIBESPAD_NATIVE_SYSTEM_PROVIDERS_H
#define TRIBESPAD_NATIVE_SYSTEM_PROVIDERS_H
#include <stdint.h>
/* Define implemented providers for one guest process; initially unloaded.
 * Call once during loader setup, before guest execution. Failure is fatal to
 * that setup; no partial configuration may be used to enter a guest. */
int tribes_system_providers_configure(uint64_t base);
/* Returns only addresses of precompiled fastcc wrappers, never call through C. */
void *tribes_system_native_lookup(uint32_t guest_address);
#endif
