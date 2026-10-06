/* Pinned Wine 10.0 locale information profile, LGPL-2.1-or-later.
   See vendor/wine-nls/LOCALE-INFO-NOTICE and LICENSE for source notices.
   Immutable source data; no registry overrides or host-localized UI names. */
#include "guest_locale_info.h"
#include "guest_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
struct InfoField {uint32_t type,kind;};
struct InfoName {const char *name;uint32_t index,lcid;};
#include "nls_info.inc"
static int range(uint64_t base,uint32_t p,uint64_t n){return p>=0x10000&&(uint64_t)p+n<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-p-n;}
static const struct InfoName *find_name(uint64_t base,uint32_t p){
 char name[86]="en-US";if(p){unsigned i;for(i=0;i<85;i++){if(!range(base,p,2ull*(i+1)))return NULL;uint16_t ch;memcpy(&ch,(void *)(uintptr_t)(base+p+2ull*i),2);if(ch>127)return NULL;name[i]=(char)ch;if(!ch)break;}if(i==85)return NULL;}
 if(!strcasecmp(name,"!x-sys-default-locale"))strcpy(name,"en-US");
 for(unsigned i=0;i<sizeof info_names/sizeof info_names[0];i++)if(!strcasecmp(name,info_names[i].name))return info_names+i;return NULL;
}
uint32_t tribes_get_locale_info_ex(uint64_t base,uint32_t lp,uint32_t type,uint32_t output,uint32_t capacity){
 if((int32_t)capacity<0||(capacity&&!output)){tribes_set_last_error(87);return 0;}
 const struct InfoName *name=find_name(base,lp);if(!name){tribes_set_last_error(87);return 0;}
 if(type&~0xf000ffffu){tribes_set_last_error(1004);return 0;}
 uint32_t key=type&0xffffu;unsigned field;
 if(type&0x10000000u){for(field=0;field<sizeof info_fields/sizeof info_fields[0];field++)if(info_fields[field].type==(key|0x10000000u))break;if(field<sizeof info_fields/sizeof info_fields[0])key|=0x10000000u;}
 for(field=0;field<sizeof info_fields/sizeof info_fields[0];field++)if(info_fields[field].type==key)break;
 if(field==sizeof info_fields/sizeof info_fields[0]){
  if(key==0x1013){fprintf(stderr,"TribesPad: locale information unsupported: localized sort description\n");abort();}
  tribes_set_last_error(1004);return 0;
 }
 uint32_t value=info_values[name->index][field],kind=info_fields[field].kind;uint16_t tmp[86];const void *data;uint32_t count;
 if(kind==0){
  if(type&0x20000000u){data=&value;count=2;}
  else{char number[16];int n=snprintf(number,sizeof number,(key==1||key==9)?"%04x":key==0x1012?"%03u":"%u",value);if(n<0||(unsigned)n>=sizeof number)abort();for(int i=0;i<n;i++)tmp[i]=(unsigned char)number[i];tmp[n]=0;data=tmp;count=(uint32_t)n+1;}
 }else{
  if(type&0x20000000u){tribes_set_last_error(1004);return 0;}
  const uint16_t *s=info_strings+value;count=s[0]+(kind==1);data=s+1;
  /* String pool uses length prefixes and no terminators, so copy a terminator
     separately. Custom-sort names are the indexed names, not base sname. */
  if((key==0x5c||key==0x7b)&&(name->lcid&0x000f0000u)){unsigned n=(unsigned)strlen(name->name);for(unsigned i=0;i<n;i++)tmp[i]=(unsigned char)name->name[i];tmp[n]=0;data=tmp;count=n+1;kind=3;}
 }
 if(!capacity)return count;
 if(capacity<count){tribes_set_last_error(122);return 0;}
 if(!range(base,output,2ull*count)){tribes_set_last_error(87);return 0;}
 if(kind==1){memcpy((void *)(uintptr_t)(base+output),data,2ull*(count-1));uint16_t zero=0;memcpy((void *)(uintptr_t)(base+output+2ull*(count-1)),&zero,2);}
 else memcpy((void *)(uintptr_t)(base+output),data,2ull*count);
 return count;
}
