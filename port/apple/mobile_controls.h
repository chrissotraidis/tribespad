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
void tb_mobile_controls_install(void);
