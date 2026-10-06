#ifndef TRIBESPAD_GUEST_SLIST_H
#define TRIBESPAD_GUEST_SLIST_H
#include <stdint.h>
/* Original 32-bit SLIST_HEADER: Next (DWORD), Depth/Sequence (WORDs).
   Initialization requires exclusive access; this does not implement push/pop. */
void tribes_initialize_slist_head(uint64_t base,uint32_t head);
#endif
