#ifndef TRIBESPAD_KEYBOARD_TRANSLATION_H
#define TRIBESPAD_KEYBOARD_TRANSLATION_H
#include <stdint.h>
uint32_t tribes_map_virtual_key(uint32_t code,uint32_t type);
uint32_t tribes_to_ascii(uint64_t base,uint32_t vk,uint32_t scan,uint32_t keys,uint32_t output,uint32_t flags);
#endif
