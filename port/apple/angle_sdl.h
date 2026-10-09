#ifndef TRIBES_ANGLE_SDL_H
#define TRIBES_ANGLE_SDL_H
#include <SDL3/SDL.h>
/* Private SDL window/context adapter. Guest pointers never enter this API. */
SDL_GLContext tribes_angle_create_context(SDL_Window *);
bool tribes_angle_destroy_context(SDL_GLContext);
bool tribes_angle_make_current(SDL_Window *,SDL_GLContext);
SDL_GLContext tribes_angle_current_context(void);
SDL_Window *tribes_angle_current_window(void);
bool tribes_angle_swap_window(SDL_Window *);
bool tribes_angle_set_swap_interval(int);
bool tribes_angle_get_swap_interval(int *);
bool tribes_angle_get_attribute(SDL_GLAttr,int *);
SDL_FunctionPointer tribes_angle_get_proc(const char *);
#ifndef TRIBES_ANGLE_IMPLEMENTATION
#define SDL_GL_CreateContext tribes_angle_create_context
#define SDL_GL_DestroyContext tribes_angle_destroy_context
#define SDL_GL_MakeCurrent tribes_angle_make_current
#define SDL_GL_GetCurrentContext tribes_angle_current_context
#define SDL_GL_GetCurrentWindow tribes_angle_current_window
#define SDL_GL_SwapWindow tribes_angle_swap_window
#define SDL_GL_SetSwapInterval tribes_angle_set_swap_interval
#define SDL_GL_GetSwapInterval tribes_angle_get_swap_interval
#define SDL_GL_GetAttribute tribes_angle_get_attribute
#define SDL_GL_GetProcAddress tribes_angle_get_proc
#endif
#endif
