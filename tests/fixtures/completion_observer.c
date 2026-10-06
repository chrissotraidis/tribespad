#include <stdatomic.h>
#include <stdint.h>
static _Atomic uint32_t watched;
static _Atomic int observed;
void completion_watch(uint32_t handle){atomic_store(&observed,0);atomic_store(&watched,handle);}
void tribes_completion_wait_observer(uint32_t handle){if(atomic_load(&watched)==handle)atomic_store(&observed,1);}
int completion_observed(void){return atomic_load(&observed);}
