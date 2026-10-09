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

/* Atomically detach the x86 Next/Depth pair and advance its 16-bit sequence.
   The returned chain still belongs to the guest; do not traverse or free it.
   https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-interlockedflushslist
   x86 header layout/sequence: wine-10.0 dlls/ntdll/sync.c RtlInterlockedFlushSList. */
uint32_t tribes_flush_slist(uint64_t base,uint32_t head){
    if(!head||(head&7u)||(uint64_t)head+8>UINT64_C(0x100000000)||base>UINTPTR_MAX-(uint64_t)head-8)abort();
    uint64_t *header=(void *)(uintptr_t)(base+head);
    uint64_t old=__atomic_load_n(header,__ATOMIC_SEQ_CST);
    for(;;){
        if(!(uint32_t)old)return 0;
        uint64_t next=((old>>48)+1)<<48;
        if(__atomic_compare_exchange_n(header,&old,next,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))return (uint32_t)old;
    }
}
