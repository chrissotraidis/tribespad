#ifndef TRIBESPAD_APPLE_CONTENT_AREA_H
#define TRIBESPAD_APPLE_CONTENT_AREA_H
#include <SDL3/SDL.h>
#include <stdbool.h>
/* Landscape phones put the Dynamic Island or notch over one side of the
   screen. The game view is inset by that safe-area width on both sides, so
   the original HUD stays visible in either landscape orientation. The guest
   sees only the inset area: display modes, window sizes, framebuffer and
   pointer coordinates all use it. Devices without side insets (iPad) see 0.
   Main thread only; the value is fixed once a landscape window exists. */
float tb_content_inset(void);
/* Pixel size of the inset game area of an SDL window. */
bool tb_content_pixels(SDL_Window *window,int *width,int *height);
/* The Metal layer the game draws into. SDL makes its Metal view the root
   view, which UIKit always stretches over the whole screen, so an inset game
   area gets its own layer inside it. Owned by the view's layer. */
void *tb_content_layer(SDL_MetalView view);
#endif
