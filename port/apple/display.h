#ifndef TRIBESPAD_APPLE_DISPLAY_H
#define TRIBESPAD_APPLE_DISPLAY_H
#include <stdint.h>
typedef struct {uint32_t width,height,native_width,native_height;double scale,native_scale;uint32_t maximum_refresh_rate;int32_t work_left,work_top,work_right,work_bottom;} TBAppleDisplay;
/* Call on the Apple UI/main thread; snapshot the primary screen. Width/height are backing-store pixels;
   native mode raster dimensions are recorded separately. Work area uses a
   top-left origin in those backing pixels (fullscreen surface on mobile). No synthesized fallback. */
int tb_apple_display_snapshot(TBAppleDisplay *output);
#endif
