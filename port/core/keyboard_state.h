#ifndef TRIBESPAD_KEYBOARD_STATE_H
#define TRIBESPAD_KEYBOARD_STATE_H
#include <stdint.h>
/* Process seed is copied once per owning thread. Apply only after a keyboard
   message is retrieved; posting/async hardware state must not call this API.
   Modifier events must specify resolved left/right virtual-key codes. */
int tribes_keyboard_configure(uint64_t base,const unsigned char initial[256]);
int tribes_keyboard_apply(uint64_t base,uint32_t vk,int down);
void tribes_keyboard_snapshot(uint64_t base,unsigned char state[256]);
uint32_t tribes_get_key_state(uint64_t base,uint32_t vk);
uint32_t tribes_get_keyboard_state(uint64_t base,uint32_t output);
#endif
