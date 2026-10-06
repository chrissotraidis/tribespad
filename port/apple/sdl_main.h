#ifndef TRIBESPAD_APPLE_SDL_MAIN_H
#define TRIBESPAD_APPLE_SDL_MAIN_H
typedef int (*TribesAppleMain)(int argc,char **argv);
/* Native application bootstrap, never a guest import. UIKit invokes entry
   after application launch; Cocoa creates its application during VIDEO init. */
int tribes_sdl_run_application(int argc,char **argv,TribesAppleMain entry);
#endif
