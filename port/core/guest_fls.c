#include "guest_fls.h"
#include "native_fls.h"
#include "guest_heap.h"
#include <stdlib.h>
#define CLEANUP_STACK_SIZE 65536u
typedef struct { _cpu cpu; uint32_t stack; } Cleanup;
static void dispatch(void *context,uint32_t callback,uint32_t value) {
    _cpu *cpu=context;
    if(cpu->_esp<128)abort();
    tribes_call_guest(cpu,callback,1,&value,cpu->_esp-128);
}
static void cleanup_dispatch(void *context,uint32_t callback,uint32_t value) {
    Cleanup *cleanup=context;dispatch(&cleanup->cpu,callback,value);
}
static void dispose(void *context) {
    Cleanup *cleanup=context;
    if(tribes_global_free(cleanup->cpu._pointer_offset,cleanup->stack))abort();
    free(cleanup);
}
static int prepare(_cpu *cpu) {
    uint32_t error=tribes_get_last_error();
    Cleanup *cleanup=tribes_fls_current_context(cpu->_pointer_offset);
    if(!cleanup) {
        cleanup=calloc(1,sizeof *cleanup);
        if(!cleanup){tribes_set_last_error(8);return 0;}
        cleanup->stack=tribes_global_alloc(cpu->_pointer_offset,0,CLEANUP_STACK_SIZE);
        if(!cleanup->stack){free(cleanup);tribes_set_last_error(8);return 0;}
        cleanup->cpu=*cpu;
        if(!tribes_fls_fiber_create(cpu->_pointer_offset,cleanup_dispatch,cleanup,dispose)) {
            dispose(cleanup);tribes_set_last_error(8);return 0;
        }
    }
    cleanup->cpu=*cpu;
    cleanup->cpu._esp=cleanup->stack+CLEANUP_STACK_SIZE;
    cleanup->cpu.stack_bottom=(void *)(uintptr_t)(cpu->_pointer_offset+cleanup->stack);
    cleanup->cpu.stack_top=(void *)(uintptr_t)(cpu->_pointer_offset+cleanup->cpu._esp);
    tribes_set_last_error(error);return 1;
}
uint32_t tribes_fls_alloc(_cpu *cpu,uint32_t callback) {
    return prepare(cpu)?tribes_fls_alloc_index(cpu->_pointer_offset,callback):UINT32_MAX;
}
uint32_t tribes_fls_get(_cpu *cpu,uint32_t index) {
    return prepare(cpu)?tribes_fls_get_value(cpu->_pointer_offset,index):0;
}
uint32_t tribes_fls_set(_cpu *cpu,uint32_t index,uint32_t value) {
    return prepare(cpu)?tribes_fls_set_value(cpu->_pointer_offset,index,value):0;
}
uint32_t tribes_fls_free(_cpu *cpu,uint32_t index) {
    return prepare(cpu)?tribes_fls_free_index(cpu->_pointer_offset,index,dispatch,cpu):0;
}
