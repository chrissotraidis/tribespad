/* CPU-aware entry to the finite host-mapped AOT loader. No file is executed,
   no code is generated, and unknown providers retain the original hard stop.
   Last-reference unmapping/reload remains an explicit mapped-image boundary. */
#include "guest_callback.h"
#include "guest_threads.h"
#include "guest_modules.h"
#include "guest_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t tribes_load_library_cpu(_cpu *cpu,uint32_t pointer){
    uint64_t base=cpu->_pointer_offset;
    if(!pointer||pointer>UINT32_MAX-4095||base>UINTPTR_MAX-pointer-4095){tribes_set_last_error(87);return 0;}
    tribes_loader_lock();uint32_t image=tribes_module_pending_image(base,(const char *)(uintptr_t)(base+pointer));
    uint32_t result=image?tribes_threads_load_image(cpu,image):tribes_load_library(base,pointer);
    tribes_loader_unlock();return result;
}
uint32_t tribes_load_library_ex_a_cpu(_cpu *cpu,uint32_t pointer,uint32_t file,uint32_t flags){
    if(file||flags)return tribes_load_library_ex_a(cpu->_pointer_offset,pointer,file,flags);
    return tribes_load_library_cpu(cpu,pointer);
}
uint32_t tribes_load_library_ex_w_cpu(_cpu *cpu,uint32_t pointer,uint32_t file,uint32_t flags){
    uint64_t base=cpu->_pointer_offset;
    if(!pointer||file||flags)return tribes_load_library_ex_w(base,pointer,file,flags);
    char name[4096];
    for(uint32_t i=0;i<sizeof name;i++){
        uint64_t at=(uint64_t)pointer+2ull*i;
        if(at>UINT32_MAX-1||base>UINTPTR_MAX-at-1){tribes_set_last_error(87);return 0;}
        uint16_t unit;memcpy(&unit,(void *)(uintptr_t)(base+at),2);
        if(unit>127){fputs("TribesPad: unsupported non-ASCII LoadLibraryExW name\n",stderr);abort();}
        name[i]=(char)unit;if(!unit){
            tribes_loader_lock();uint32_t image=tribes_module_pending_image(base,name);
            uint32_t result=image?tribes_threads_load_image(cpu,image):tribes_load_library_ex_w(base,pointer,file,flags);
            tribes_loader_unlock();return result;
        }
    }
    tribes_set_last_error(126);return 0;
}
uint32_t tribes_free_library_cpu(_cpu *cpu,uint32_t image){
    tribes_loader_lock();uint32_t result=tribes_free_library(cpu->_pointer_offset,image);tribes_loader_unlock();return result;
}
