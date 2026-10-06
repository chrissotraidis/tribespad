#ifndef TRIBESPAD_GUEST_CURSORS_H
#define TRIBESPAD_GUEST_CURSORS_H
#include <stdint.h>
/* Native software cursor theme, with immutable owned pixels and a shared handle.
   The standard arrow is implemented; custom resources and other system shapes
   remain strict gates. Apple pointer installation/display is separate. */
uint32_t tribes_load_cursor(uint64_t base,uint32_t module,uint32_t name);
typedef struct {uint32_t width,height,hotspot_x,hotspot_y,system_id;const unsigned char *rgba,*and_mask;} TribesCursorView;
int tribes_cursor_view(uint64_t base,uint32_t handle,TribesCursorView *view);
#endif
