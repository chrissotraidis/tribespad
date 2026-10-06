#include "sdl_main.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <pthread.h>
#include <stdlib.h>
extern void tribes_sdl_host_application_ready(void);
extern void tribes_sdl_host_application_finished(void);
static TribesAppleMain application_entry;
static int application_main(int argc,char **argv) {
    tribes_sdl_host_application_ready();
    int result=application_entry(argc,argv);
    /* UIKit continues its native lifecycle after the application entry returns.
       The checked runtime must be fully shut down before its guest callbacks
       are detached. Native post-main SDL error/TLS handling then owns its own
       allocations; it must not call an expired guest CPU/allocator frame. */
    tribes_sdl_host_application_finished();
    SDL_malloc_func allocate;SDL_calloc_func allocate_zero;
    SDL_realloc_func resize;SDL_free_func release;
    SDL_GetOriginalMemoryFunctions(&allocate,&allocate_zero,&resize,&release);
    if(!SDL_SetMemoryFunctions(allocate,allocate_zero,resize,release))abort();
    SDL_SetLogOutputFunction(NULL,NULL);
    return result;
}
int tribes_sdl_run_application(int argc,char **argv,TribesAppleMain entry) {
    if(!pthread_main_np()||!entry||application_entry)abort();
    application_entry=entry;
    return SDL_RunApp(argc,argv,application_main,NULL);
}
