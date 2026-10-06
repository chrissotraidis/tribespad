#include "guest_slist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The x86 guest header is eight bytes even on a 64-bit Apple host.
   Original guest alignment is eight, rather than Win64's sixteen.
   https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-initializeslisthead
   https://github.com/wine-mirror/wine/blob/wine-10.0/include/winnt.h */
void tribes_initialize_slist_head(uint64_t base,uint32_t head) {
    if(!head||(head&7u)||(uint64_t)head+8>UINT64_C(0x100000000)||base>UINTPTR_MAX-(uint64_t)head-8) {
        fprintf(stderr,"TribesPad: invalid x86 SLIST_HEADER address %08x\n",head);abort();
    }
    memset((void *)(uintptr_t)(base+head),0,8);
}
