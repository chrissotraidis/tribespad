#pragma once
#include "../core/input.h"
#include <SDL3/SDL.h>
typedef struct { TBInput input; uint32_t held; SDL_WindowID window; } TBMobileControls;
/* Original controlDefaults.cs uses ESDF, space, W, G and R. */
void tb_mobile_controls_emit(TBMobileControls *controls);
void tb_mobile_controls_release(TBMobileControls *controls);
void tb_mobile_controls_install(void);
