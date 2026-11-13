#pragma once
#include "../core/input.h"
#include <SDL3/SDL.h>
typedef struct { TBInput input; uint32_t held; SDL_WindowID window; float carryX,carryY; } TBMobileControls;
/* Original controlDefaults.cs uses ESDF, space, W, G and R. */
void tb_mobile_controls_emit(TBMobileControls *controls);
void tb_mobile_controls_release(TBMobileControls *controls);
/* Source 9 belongs to the active gamepad; stick aim is time based. speed is
   the full-deflection turn rate in mouse units per second. */
void tb_mobile_controls_gamepad_look(TBMobileControls *controls,float x,float y,double seconds,float speed);
/* Whether the touch controls show (1) beside a controller. It hides them while
   in use (Tribes 2: once used; Starsiege and Vengeance: while connected) and
   Auto-hide Touch Controls with Controller is on; when it stops hiding them,
   in a match and with no mouse in use, the ones it put away come back. */
int tb_touch_shown_for_controller(int shown,int was_hiding,int hiding,int in_match,int mouse_used);
void tb_mobile_controls_install(void);
/* The nine keys the touch buttons send (see mobile_controls.c); NULL for Tribes 2's.
   TB_KEY_WHEEL_DOWN sends one mouse wheel step down on press instead of a key. */
#define TB_KEY_WHEEL_DOWN ((SDL_Scancode)SDL_SCANCODE_COUNT)
void tb_mobile_controls_keys(const SDL_Scancode keys[9]);
/* Touch controls for Starsiege: Tribes: its keys, menu and play state. */
void tb_mobile_controls_install_starsiege(void);
/* Touch controls for Tribes: Vengeance: its keys and menu. */
void tb_mobile_controls_install_vengeance(void);
/* Vengeance: Ski / Jump taps the jetpack for 90 ms on press, then skis
   (mobile_controls.c); emit again after 90 ms to end the tap. */
void tb_mobile_controls_jump_taps_jet(int on);
int tb_mobile_controls_jumping(void);
