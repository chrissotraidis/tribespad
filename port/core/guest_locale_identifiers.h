#ifndef TRIBESPAD_GUEST_LOCALE_IDENTIFIERS_H
#define TRIBESPAD_GUEST_LOCALE_IDENTIFIERS_H
#include <stdint.h>
uint32_t tribes_get_user_default_locale_name(uint64_t base,uint32_t output,uint32_t capacity);
uint32_t tribes_is_valid_locale_name(uint64_t base,uint32_t name);
uint32_t tribes_locale_name_to_lcid(uint64_t base,uint32_t name,uint32_t flags);
uint32_t tribes_lcid_to_locale_name(uint64_t base,uint32_t lcid,uint32_t output,uint32_t capacity,uint32_t flags);
#endif
