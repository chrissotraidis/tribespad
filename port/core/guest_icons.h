#ifndef TRIBESPAD_GUEST_ICONS_H
#define TRIBESPAD_GUEST_ICONS_H
#include <stdint.h>
/* Immutable mapped-PE resource snapshot. Default legacy icon size is 32x32.
   Shared icons live until guest-process teardown (teardown not yet provided).
   Integer resource identifiers and uncompressed Windows DIBs are supported. */
int tribes_icons_configure(uint64_t base,uint32_t module,const void *mapped_image,uint32_t size);
uint32_t tribes_load_icon(uint64_t base,uint32_t module,uint32_t name);
typedef struct {uint32_t width,height,resource_id,bits_per_pixel,uses_alpha;const unsigned char *rgba,*and_mask;} TribesIconView;
/* Borrowed immutable pixels remain valid for the shared resource's lifetime.
   and_mask has one byte per pixel. Retain XOR colors even where AND is set:
   legacy composition is (destination & AND) ^ XOR, including inversion.
   uses_alpha selects straight source-alpha composition for 32-bit alpha icons. */
int tribes_icon_view(uint64_t base,uint32_t handle,TribesIconView *view);
#endif
