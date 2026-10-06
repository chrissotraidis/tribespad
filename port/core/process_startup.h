#ifndef TRIBESPAD_PROCESS_STARTUP_H
#define TRIBESPAD_PROCESS_STARTUP_H
#include <stdint.h>
/* Configure immutable Win32 launch metadata before guest execution. */
int tribes_startup_configure(uint64_t base,uint16_t show,const uint32_t handles[3]);
void tribes_get_startup_info(uint64_t base,uint32_t output);
uint32_t tribes_set_handle_count(uint32_t requested);
#endif
