#define tribes_lookup sdl_common_lookup
#include "sdl_hint_dispatch.c"
#undef tribes_lookup
extern void tbimp_SDL_GetKeyboardState(void),tbimp_SDL_GetGamepads(void),tbimp_SDL_HasJoystick(void);
extern void tbimp_SDL_OpenGamepad(void),tbimp_SDL_GetGamepadName(void),tbimp_SDL_CloseGamepad(void),tbimp_TestGuestFree(void);
void *tribes_lookup(uint32_t pc){
 switch(pc){
 case 30:return tbimp_SDL_GetKeyboardState;
 case 31:return tbimp_SDL_GetGamepads;
 case 32:return tbimp_SDL_HasJoystick;
 case 33:return tbimp_SDL_OpenGamepad;
 case 34:return tbimp_SDL_GetGamepadName;
 case 35:return tbimp_SDL_CloseGamepad;
 case 36:return tbimp_TestGuestFree;
 default:return sdl_common_lookup(pc);
 }
}
