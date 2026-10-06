#include "guest_once_callback.h"
#include "guest_once.h"
#include "guest_heap.h"
#include <stdlib.h>
#include <string.h>
uint32_t tribes_init_once_execute(_cpu *cpu,uint32_t once,uint32_t callback,uint32_t parameter,uint32_t context){
    if(!cpu||!callback||(context&&(context>UINT32_MAX-3||cpu->_pointer_offset>UINTPTR_MAX-context-3))){tribes_set_last_error(87);return 0;}
    if(context&&(uint64_t)context<once+UINT64_C(4)&&(uint64_t)once<context+UINT64_C(4))abort();
    uint32_t value=0,state=tribes_once_begin(cpu->_pointer_offset,once,&value);if(!state)return 0;
    if(state==1){
        if(cpu->_esp<256||cpu->_pointer_offset>UINTPTR_MAX-cpu->_esp)abort();
        uint32_t scratch=cpu->_esp-128;
        if(cpu->stack_bottom&&cpu->_pointer_offset+scratch<(uintptr_t)cpu->stack_bottom)abort();
        memcpy((void *)(uintptr_t)(cpu->_pointer_offset+scratch),&value,4);
        uint32_t args[]={once,parameter,scratch};uint32_t result=tribes_call_guest(cpu,callback,3,args,scratch-64);
        memcpy(&value,(void *)(uintptr_t)(cpu->_pointer_offset+scratch),4);
        if(!tribes_once_complete(cpu->_pointer_offset,once,result,value))return 0;
    }
    if(context)memcpy((void *)(uintptr_t)(cpu->_pointer_offset+context),&value,4);return 1;
}
