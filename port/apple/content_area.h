#ifndef TRIBESPAD_APPLE_CONTENT_AREA_H
#define TRIBESPAD_APPLE_CONTENT_AREA_H
#include <SDL3/SDL.h>
#include <stdbool.h>
/* The game fills the whole screen. With the TribesPad.avoidNotch setting,
   landscape phones inset the game by the notch or Dynamic Island width on
   both sides instead, so the HUD stays visible in either orientation. The
   guest sees only the game area: display modes, window sizes, framebuffer
   and pointer coordinates all use it. Devices without side insets see 0.
   Main thread only; the value is fixed once a landscape window exists. */
float tb_content_inset(void);
/* Guest pixels per point on phones (see content_area.m), 0 elsewhere. */
float tb_content_density(void);
/* Pixel size of the inset game area of an SDL window. */
bool tb_content_pixels(SDL_Window *window,int *width,int *height);
/* The Metal layer the game draws into. SDL makes its Metal view the root
   view, which UIKit always stretches over the whole screen, so an inset game
   area gets its own layer inside it. Owned by the view's layer. */
void *tb_content_layer(SDL_MetalView view);
/* Starsiege: Tribes draws a fixed-size frame (800x600). Called before its
   window exists, this makes the game area that many pixels, 4:3 and centred,
   for the system to scale to the screen. Tribes 2 never calls it. */
void tb_content_fix_game_size(int width,int height);
/* Reapply the app audio session after SDL closes an audio device (mobile_engine_main.m). */
void tb_audio_session_restore(void);
#endif
