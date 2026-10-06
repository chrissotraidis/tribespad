/* Win32 identity reads share the configured guest FS/TEB contract. */
#include "guest_teb.h"

uint32_t tribes_get_current_process_id(void) {return x86_read_fs_dword(0x20);}
uint32_t tribes_get_current_thread_id(void) {return x86_read_fs_dword(0x24);}
