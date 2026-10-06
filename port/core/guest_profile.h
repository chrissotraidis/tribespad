#ifndef TRIBESPAD_GUEST_PROFILE_H
#define TRIBESPAD_GUEST_PROFILE_H
#include <stdint.h>
/* Explicit file-backed virtual Win.ini profile, with no IniFileMapping entries.
   Native configuration is immutable per guest address space. */
int tribes_profile_configure_file(uint64_t base,const char *guest_path);
uint32_t tribes_get_profile_string_a(uint64_t base,uint32_t section,uint32_t key,uint32_t fallback,uint32_t output,uint32_t capacity);
#endif
