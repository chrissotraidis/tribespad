#define tribes_lookup sdl_common_lookup
#include "sdl_hint_dispatch.c"
#undef tribes_lookup
#define WINDOW_APIS(X) \
 X(40,CreateWindow) X(41,DestroyWindow) X(42,ShowWindow) X(43,GetWindowID) X(44,GetWindowProperties) \
 X(45,GetWindowSize) X(46,GetWindowSizeInPixels) X(47,GL_CreateContext) X(48,GL_DestroyContext) \
 X(49,GL_MakeCurrent) X(50,GL_GetCurrentWindow) X(51,GL_GetCurrentContext) X(52,GL_SwapWindow) \
 X(53,GL_SetAttribute) X(54,GL_GetAttribute) X(55,GL_ResetAttributes) X(56,GetFullscreenDisplayModes) X(65,CreateProperties) X(66,SetStringProperty) X(67,SetBooleanProperty) X(68,SetNumberProperty) X(69,CreateWindowWithProperties) X(70,DestroyProperties) X(71,GetDisplayForWindow) X(72,GetCurrentDisplayMode) X(73,SetWindowFullscreenMode) X(74,GL_SetSwapInterval) X(75,GetWindowDisplayScale) X(76,ceilf) X(77,GetClosestFullscreenDisplayMode) X(78,SetWindowFullscreen) X(79,SetWindowSize) X(80,SetWindowBordered) X(81,SetWindowPosition) X(82,SyncWindow) X(83,CreateSurfaceFrom) X(84,ConvertSurface) X(85,PremultiplySurfaceAlpha) X(86,DestroySurface) X(87,CreateColorCursor) X(88,SetCursor) X(89,DestroyCursor) X(90,PollEvent) X(91,PumpEvents) X(92,GetKeyboardState) X(93,RemoveSurfaceAlternateImages) X(94,SetWindowRelativeMouseMode)
#define DECLARE(id,name) extern void tbimp_SDL_##name(void);
WINDOW_APIS(DECLARE)
#ifdef GRAPHICS
#include "../../port/core/guest_graphics.h"
extern void tbimp_SDL_LoadObject(void),tbimp_SDL_LoadFunction(void),tbimp_SDL_UnloadObject(void),tbimp_SDL_GL_GetProcAddress(void),tbimp_SDL_GetPointerProperty(void);
#endif
extern void tbimp_TestGuestFree(void);
void *tribes_lookup(uint32_t pc){if(pc==57)return tbimp_TestGuestFree;
#ifdef GRAPHICS
void *graphics=tribes_graphics_native_lookup(pc);if(graphics)return graphics;
switch(pc){case 60:return tbimp_SDL_LoadObject;case 61:return tbimp_SDL_LoadFunction;case 62:return tbimp_SDL_UnloadObject;case 63:return tbimp_SDL_GL_GetProcAddress;case 64:return tbimp_SDL_GetPointerProperty;}
#endif
switch(pc){
#define ROUTE(id,name) case id:return tbimp_SDL_##name;
WINDOW_APIS(ROUTE)
default:return sdl_common_lookup(pc);}}
