#include "guest_sdl_allocator.h"
#include "guest_sdl_memory.h"
#include "guest_callback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
/* SDL 3.2.18 SDL_stdlib.c, using i686 size_t/pointer widths. Keep the
   registered original allocator and its matching free callback. */
uint32_t tribes_sdl_aligned_alloc(_cpu *cpu,uint32_t alignment,uint32_t size){
    if(alignment<4)alignment=4;
    uint32_t padding=alignment-size%alignment;
    uint64_t total=(uint64_t)size+alignment+4+padding;
    if(total>UINT32_MAX)return 0;
    uint32_t original=tribes_sdl_malloc(cpu,(uint32_t)total);
    if(!original)return 0;
    uint64_t result=(uint64_t)original+4;
    result+=alignment-result%alignment;
    if((uint64_t)original+total>UINT64_C(0x100000000)||result>UINT32_MAX)abort();
    memcpy((void *)(uintptr_t)(cpu->_pointer_offset+result-4),&original,4);
    memset((void *)(uintptr_t)(cpu->_pointer_offset+result+size),0,padding);
    return (uint32_t)result;
}
void tribes_sdl_aligned_free(_cpu *cpu,uint32_t pointer){
    if(!pointer)return;
    if(pointer<4)abort();
    uint32_t original;
    memcpy(&original,(void *)(uintptr_t)(cpu->_pointer_offset+pointer-4),4);
    tribes_sdl_free(cpu,original);
}
