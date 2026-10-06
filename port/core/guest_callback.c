#include "guest_callback.h"
#include <stdlib.h>
#include <string.h>
extern void tribes_enter(_cpu *,uint32_t);
static _Thread_local _cpu *current;
typedef struct Finalizer { TribesGuestFinalizer function;struct Finalizer *next; } Finalizer;
static _Thread_local Finalizer *finalizers;
int tribes_guest_finalizer_register(TribesGuestFinalizer function) {
    if(!function)abort();
    for(Finalizer *f=finalizers;f;f=f->next)if(f->function==function)return 1;
    Finalizer *f=malloc(sizeof *f);if(!f)return 0;
    f->function=function;f->next=finalizers;finalizers=f;return 1;
}
void tribes_guest_thread_finalize(_cpu *cpu) {
    unsigned iterations=0;
    while(finalizers) {
        if(++iterations>64)abort();
        Finalizer *f=finalizers;finalizers=f->next;
        TribesGuestFinalizer function=f->function;free(f);function(cpu);
    }
}
_cpu *tribes_guest_callback_context(void){return current;}
void tribes_guest_callback_reset(void){current=NULL;}
static uint32_t call_guest(_cpu *parent,uint32_t target,uint32_t count,const uint32_t *arguments,uint32_t stack_top,int cdecl) {
    if(!parent||count>16||(count&&!arguments)||stack_top>parent->_esp||stack_top<4*(count+1))abort();
    uint64_t address=parent->_pointer_offset+(uint64_t)stack_top-4*(count+1);
    if(address<parent->_pointer_offset||address>UINTPTR_MAX-4*(count+1))abort();
    if(parent->stack_bottom&&address<(uintptr_t)parent->stack_bottom)abort();
    if(parent->stack_top&&parent->_pointer_offset+stack_top>(uintptr_t)parent->stack_top)abort();
    _cpu child=*parent;child._esp=stack_top-4*count;
    if(count)memcpy((void *)(uintptr_t)(child._pointer_offset+child._esp),arguments,4*count);
    _cpu *previous=current;current=&child;
    tribes_enter(&child,target);
    current=previous;
    /* stdcall consumes arguments; cdecl leaves them for its native caller. */
    if(child._esp!=(cdecl?stack_top-4*count:stack_top))abort();
    /* General registers/flags stay in the API caller; x87/MMX is thread state. */
    parent->_st_top=child._st_top;parent->_st_sw_cond=child._st_sw_cond;parent->_st_cw=child._st_cw;
    memcpy(parent->_st,child._st,sizeof child._st);parent->_st_result=child._st_result;
    parent->_st_tag=child._st_tag;parent->_st_mmx_active=child._st_mmx_active;
    memcpy(parent->_st_exp,child._st_exp,sizeof child._st_exp);parent->_st_extended=child._st_extended;
    memcpy(parent->_xmm,child._xmm,sizeof child._xmm);parent->_mxcsr=child._mxcsr;
    memcpy(parent->_ymm_hi,child._ymm_hi,sizeof child._ymm_hi);
    return child._eax;
}

uint32_t tribes_call_guest(_cpu *parent,uint32_t target,uint32_t count,const uint32_t *arguments,uint32_t stack_top) {
    return call_guest(parent,target,count,arguments,stack_top,0);
}
uint32_t tribes_call_guest_cdecl(_cpu *parent,uint32_t target,uint32_t count,const uint32_t *arguments,uint32_t stack_top) {
    return call_guest(parent,target,count,arguments,stack_top,1);
}
