/* System FormatMessage output in original guest addresses. The registered
   English catalog is inherited from the pinned ProjectReach reference.
   https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessagea
   Guest templates, module resources, inserts and wrapping remain explicit gates. */
#include "guest_messages.h"
#include "guest_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system_messages.inc"
static uint32_t failure(uint32_t error){tribes_set_last_error(error);return 0;}
static int range(uint64_t base,uint32_t address,uint64_t bytes){return address&&bytes&&bytes<=UINT64_C(0x100000000)-address&&base<=UINTPTR_MAX-address-(bytes-1);}
static uint32_t format(uint64_t base,uint32_t flags,uint32_t source,uint32_t id,uint32_t language,uint32_t output,uint32_t capacity,uint32_t arguments,unsigned width){
 (void)source;(void)arguments; /* FROM_SYSTEM ignores source; catalog has no inserts. */
 if((flags&0x400)&&((flags&0x800)||(flags&0x1000)))return failure(87);
 if((flags&~0x33ffu)||!(flags&0x1000)||((flags&255)!=0&&(flags&255)!=255)){
  fprintf(stderr,"TribesPad: unsupported FormatMessage flags 0x%x\n",flags);abort();
 }
 if(language!=0&&language!=0x400&&language!=0x800&&language!=0x409&&language!=9)return failure(1815);
 const char *text=NULL;
 /* HRESULT_FROM_WIN32 uses the same system error message identifier. */
 if((id>>16)==0x8007)id&=0xffff;
 for(unsigned i=0;i<sizeof system_messages/sizeof system_messages[0];i++)if(system_messages[i].id==id){text=system_messages[i].text;break;}
 if(!text){fprintf(stderr,"TribesPad: unsupported system message catalog ID %u\n",id);abort();}
 size_t length=strlen(text);if((flags&255)==255){/* trim the catalog's ordinary final CRLF */
  if(length>=2&&text[length-2]=='\r'&&text[length-1]=='\n')length-=2;
 }
 uint64_t bytes=(length+1)*width;uint32_t destination=output;
 if(flags&0x100){
  if(!range(base,output,4))return failure(87);
  uint64_t minimum=(uint64_t)capacity*width;if(minimum>bytes)bytes=minimum;
  if(bytes>UINT32_MAX)return failure(8);
  destination=tribes_global_alloc(base,0,(uint32_t)bytes);if(!destination)return 0;
 }else{
  if(capacity<=length)return failure(122);
  if(!range(base,output,bytes))return failure(87);
 }
 if(width==1){memcpy((void *)(uintptr_t)(base+destination),text,length);*(unsigned char *)(uintptr_t)(base+destination+length)=0;}
 else for(size_t i=0;i<=length;i++){uint16_t unit=i<length?(unsigned char)text[i]:0;memcpy((void *)(uintptr_t)(base+destination+2*i),&unit,2);}
 if(flags&0x100)memcpy((void *)(uintptr_t)(base+output),&destination,4);
 return (uint32_t)length;
}
uint32_t tribes_format_message_a(uint64_t b,uint32_t f,uint32_t s,uint32_t i,uint32_t l,uint32_t o,uint32_t n,uint32_t a){return format(b,f,s,i,l,o,n,a,1);}
uint32_t tribes_format_message_w(uint64_t b,uint32_t f,uint32_t s,uint32_t i,uint32_t l,uint32_t o,uint32_t n,uint32_t a){return format(b,f,s,i,l,o,n,a,2);}
