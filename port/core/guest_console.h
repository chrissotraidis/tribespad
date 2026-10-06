#ifndef TRIBESPAD_GUEST_CONSOLE_H
#define TRIBESPAD_GUEST_CONSOLE_H
#include <stdint.h>
/* Process-owned console registration. Nonzero Add is TRUE; NULL toggles Ctrl+C
   ignore. Duplicate registrations are retained; removal deletes the newest. */
uint32_t tribes_set_console_ctrl_handler(uint64_t base,uint32_t handler,uint32_t add);
typedef struct TribesConsoleDispatch TribesConsoleDispatch;
/* Snapshot semantics: subsequent registry mutations affect later events only.
   begin also supports the initial empty/default list without registration. */
int tribes_console_begin(uint64_t base,uint32_t event,TribesConsoleDispatch **dispatch);
uint32_t tribes_console_next(TribesConsoleDispatch *dispatch);
int tribes_console_ignored(const TribesConsoleDispatch *dispatch);
void tribes_console_end(TribesConsoleDispatch *dispatch);
void tribes_console_destroy(uint64_t base);
#endif
