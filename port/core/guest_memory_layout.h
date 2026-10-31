#ifndef TRIBESPAD_GUEST_MEMORY_LAYOUT_H
#define TRIBESPAD_GUEST_MEMORY_LAYOUT_H
#include <stdint.h>
/* The guest's dynamic memory windows: GlobalAlloc from 0x30000000 (span
   tribes_global_heap_span), the process heap at tribes_process_heap_low (256 MiB),
   a 192 KiB hole, then VirtualAlloc from tribes_virtual_memory_low to 0x7fff0000.
   The defaults are Tribes 2's (512 MiB, 0x50000000, 0x60030000). A host that needs
   more VirtualAlloc room (Tribes: Vengeance) sets all three, contiguous in that
   order, before the guest's first allocation. */
extern uint32_t tribes_global_heap_span,tribes_process_heap_low,tribes_virtual_memory_low;
#endif
