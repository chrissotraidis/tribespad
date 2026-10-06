#include "guest_sdl_allocator.h"
#include "guest_sdl_memory.h"
#include "guest_callback.h"
#include <stdio.h>
#include <stdlib.h>
static uint32_t invoke(_cpu *cpu,unsigned index,unsigned count,const uint32_t *arguments){
    uint32_t functions[4];
    if(!cpu||cpu->_esp<128||!tribes_sdl_memory_functions(cpu->_pointer_offset,functions)){
        fputs("TribesPad: SDL guest allocator requires registered callbacks and guest stack\n",stderr);abort();
    }
    return tribes_call_guest_cdecl(cpu,functions[index],count,arguments,cpu->_esp-128);
}
uint32_t tribes_sdl_malloc(_cpu *cpu,uint32_t size){uint32_t arg=size?size:1;uint32_t result=invoke(cpu,0,1,&arg);if(!result)tribes_sdl_memory_out_of_memory();return result;}
uint32_t tribes_sdl_calloc(_cpu *cpu,uint32_t count,uint32_t size){uint32_t args[]={count,size};if(!count||!size)args[0]=args[1]=1;uint32_t result=invoke(cpu,1,2,args);if(!result)tribes_sdl_memory_out_of_memory();return result;}
uint32_t tribes_sdl_realloc(_cpu *cpu,uint32_t pointer,uint32_t size){uint32_t args[]={pointer,size?size:1};uint32_t result=invoke(cpu,2,2,args);if(!result)tribes_sdl_memory_out_of_memory();return result;}
void tribes_sdl_free(_cpu *cpu,uint32_t pointer){if(pointer)(void)invoke(cpu,3,1,&pointer);}
