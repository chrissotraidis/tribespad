#ifndef TRIBESPAD_GUEST_LOCALES_H
#define TRIBESPAD_GUEST_LOCALES_H
#include <stdint.h>
/* Immutable pinned locale profile; index is enumeration-local, never global. */
uint32_t tribes_locale_enum_validate(uint32_t callback,uint32_t flags,uint32_t reserved);
uint32_t tribes_locale_next(uint32_t wanted,uint32_t *index,uint16_t name[85],uint32_t *flags);
#endif
