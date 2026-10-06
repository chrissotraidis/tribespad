/* Stable per-CPU guest buffers for inherited wide x87 helper results.
   Runtime-owned 0x2f000000..0x30000000 storage is distinct from the PE, stack
   and 0x30000000..0x50000000 application heap; pages are never executable. */
#include "llasm_cpu.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
/* Standalone arithmetic tests may omit the loader service. The strong loader
   definition replaces this non-inlined fallback in complete runtime links. */
__attribute__((weak,noinline)) uint32_t tribes_address_space_commit_scratch(uint64_t base,uint32_t address,uint32_t size){
 long page=sysconf(_SC_PAGESIZE);
 if(page<4096||page>65536||(page&(page-1))||size!=(uint32_t)page||(base|address)%size||
    address<0x2f000000||(uint64_t)address+size>0x30000000||base>UINTPTR_MAX-UINT32_C(0x30000000))return 0;
 return !mprotect((void *)(uintptr_t)(base+address),size,PROT_READ|PROT_WRITE);
}
typedef struct Arena { uint64_t base;uint32_t next;struct Arena *link; } Arena;
typedef struct Slot { _cpu *owner;uint64_t base;uint32_t address;struct Slot *link; } Slot;
static Arena *arenas;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local Slot *slots;
uint32_t halopad_x87_result(CPU,const int64_t *value) {
    Slot *slot=slots;while(slot&&(slot->owner!=cpu||slot->base!=cpu->_pointer_offset))slot=slot->link;
    if(!slot) {
        slot=calloc(1,sizeof(*slot));if(!slot)abort();pthread_mutex_lock(&lock);
        Arena *arena=arenas;while(arena&&arena->base!=cpu->_pointer_offset)arena=arena->link;
        if(!arena) { arena=calloc(1,sizeof(*arena));if(!arena)abort();arena->base=cpu->_pointer_offset;arena->next=0x2f000000;arena->link=arenas;arenas=arena; }
        if(arena->next>0x2ffffff0){fprintf(stderr,"TribesPad: x87 result arena exhausted\n");abort();}
        long page=sysconf(_SC_PAGESIZE);if(page<4096||page>65536||(page&(page-1)))abort();uint32_t address=arena->next;
        uint64_t lo=(uint64_t)address&~((uint64_t)page-1);
        if(arena->base>UINTPTR_MAX-UINT32_C(0x30000000))abort();
        if(!tribes_address_space_commit_scratch(arena->base,(uint32_t)lo,(uint32_t)page))abort();
        arena->next+=16;pthread_mutex_unlock(&lock);slot->owner=cpu;slot->base=cpu->_pointer_offset;slot->address=address;slot->link=slots;slots=slot;
    }
    memcpy((void *)(uintptr_t)(slot->base+slot->address),value,8);return slot->address;
}
