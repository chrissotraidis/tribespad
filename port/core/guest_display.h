#ifndef TRIBESPAD_GUEST_DISPLAY_H
#define TRIBESPAD_GUEST_DISPLAY_H
#include <stdint.h>
#include <stddef.h>
/* Main Apple backing-store pixels; BGRA8 software desktop surface. No host
   desktop capture, onscreen drawing or rotation/multiple-display updates yet. */
int tribes_display_configure(uint64_t base,uint32_t width,uint32_t height);
/* Trusted initial primary work rectangle, top-left origin in backing pixels. */
int tribes_display_configure_work_area(uint64_t base,int32_t left,int32_t top,int32_t right,int32_t bottom);
uint32_t tribes_monitor_from_window(uint64_t base,uint32_t window,uint32_t flags);
uint32_t tribes_get_monitor_info_a(uint64_t base,uint32_t monitor,uint32_t output);
uint32_t tribes_get_monitor_info_w(uint64_t base,uint32_t monitor,uint32_t output);
uint32_t tribes_get_desktop_window(uint64_t base);
uint32_t tribes_is_desktop_window(uint64_t base,uint32_t window);
uint32_t tribes_get_window_dc(uint64_t base,uint32_t window);
uint32_t tribes_release_window_dc(uint64_t base,uint32_t window,uint32_t dc);
uint32_t tribes_get_system_metrics(uint64_t base,uint32_t index);
uint32_t tribes_get_display_caps(uint64_t base,uint32_t dc,uint32_t index);
int tribes_display_surface_snapshot(uint64_t base,uint32_t dc,uint32_t *width,uint32_t *height,void *pixels,size_t capacity);
#endif
