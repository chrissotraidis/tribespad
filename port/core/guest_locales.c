#include "guest_locales.h"
#include "guest_heap.h"
#include <stddef.h>
struct LocaleRow {const char *name;uint32_t flags;};
#include "nls_locales.inc"
uint32_t tribes_locale_enum_validate(uint32_t callback,uint32_t flags,uint32_t reserved){
 if(!callback||reserved){tribes_set_last_error(87);return 0;}
 if(flags&~0x37u){tribes_set_last_error(1004);return 0;}return 1;
}
uint32_t tribes_locale_next(uint32_t wanted,uint32_t *index,uint16_t name[85],uint32_t *flags){
 if(!index||!name||!flags||wanted&~0x37u){tribes_set_last_error(87);return 0;}
 while(*index<sizeof locale_rows/sizeof locale_rows[0]){
  const struct LocaleRow *r=locale_rows+(*index)++;if(wanted&&!(r->flags&wanted))continue;
  unsigned i=0;do{name[i]=(unsigned char)r->name[i];}while(name[i++]);*flags=r->flags;return 1;
 }return 0;
}
