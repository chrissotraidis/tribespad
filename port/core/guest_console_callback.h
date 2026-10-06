#ifndef TRIBESPAD_GUEST_CONSOLE_CALLBACK_H
#define TRIBESPAD_GUEST_CONSOLE_CALLBACK_H
#include "guest_callback.h"
enum {TRIBES_CONSOLE_ERROR=0,TRIBES_CONSOLE_HANDLED=1,TRIBES_CONSOLE_IGNORED=2,TRIBES_CONSOLE_TERMINATE=4};
/* Called by the host on a dedicated guest thread with its own CPU/stack/TEB.
   Never call in a POSIX signal handler or reuse a running guest thread context.
   TERMINATE requests guest-process exit, including handled close/logoff/shutdown;
   the host MUST consume it. This helper never exits the containing Apple app.
   No attach/allocate-console, debugger policy, inheritance or timeout emulation. */
uint32_t tribes_console_dispatch(_cpu *cpu,uint32_t event);
/* Opt-in desktop console source translation after sigwait: INT=C, QUIT=BREAK,
   HUP=CLOSE. No process-global signal handlers or masks are installed here.
   iOS background/termination is not a Windows console event. */
uint32_t tribes_console_deliver_posix(_cpu *cpu,int signal_number);
#endif
