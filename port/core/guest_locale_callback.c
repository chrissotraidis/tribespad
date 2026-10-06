#include "guest_locale_callback.h"
#include "guest_locales.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static _Noreturn void unsupported(const char *why){fprintf(stderr,"TribesPad: locale callback unsupported: %s\n",why);abort();}
uint32_t tribes_enum_system_locales_ex(_cpu *cpu,uint32_t callback,uint32_t wanted,uint32_t parameter,uint32_t reserved){
 if(!tribes_locale_enum_validate(callback,wanted,reserved))return 0;
 if(!cpu||cpu->_esp<512||cpu->_pointer_offset>UINTPTR_MAX-cpu->_esp)unsupported("invalid stack");
 uint32_t scratch=cpu->_esp-256,top=scratch-64;
 if(cpu->stack_bottom&&cpu->_pointer_offset+top-16<(uintptr_t)cpu->stack_bottom)unsupported("callback stack below bound");
 if(cpu->stack_top&&cpu->_pointer_offset+scratch+170>(uintptr_t)cpu->stack_top)unsupported("name scratch above bound");
 uint32_t index=0,flags;uint16_t name[85];
 while(tribes_locale_next(wanted,&index,name,&flags)){
  unsigned count=0;while(name[count])count++;count++;
  memcpy((void *)(uintptr_t)(cpu->_pointer_offset+scratch),name,count*2);
  uint32_t args[]={scratch,flags,parameter};if(!tribes_call_guest(cpu,callback,3,args,top))break;
 }return 1;
}
