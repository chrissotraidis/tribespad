/* Continuable software notification dispatch through original x86 SEH frames.
   Only the reached thread-name notification is admitted; no native faults,
   unwinding, context edits or arbitrary exceptions are silently continued. */
#include "guest_callback.h"
#include "guest_teb.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static _Noreturn void stop(const char *why){fprintf(stderr,"TribesPad: unsupported RaiseException: %s\n",why);abort();}
static void *guest(_cpu *c,uint32_t a,uint32_t n){if(!a||(uint64_t)a+n>UINT64_C(0x100000000)||c->_pointer_offset>UINTPTR_MAX-a-(n-1))stop("address range");return (void *)(uintptr_t)(c->_pointer_offset+a);}
static int stack(_cpu *c,uint32_t a,uint32_t n){uint64_t p=c->_pointer_offset+(uint64_t)a;return a&&p>=c->_pointer_offset&&p<=UINTPTR_MAX-n&&c->stack_bottom&&c->stack_top&&p>=(uintptr_t)c->stack_bottom&&p+n<=(uintptr_t)c->stack_top;}
void tribes_raise_exception(_cpu *cpu,uint32_t code,uint32_t flags,uint32_t count,uint32_t arguments){
 if(code!=0x406d1388||flags||count!=4)stop("exception code/flags/argument count");
 uint32_t packet[4];memcpy(packet,guest(cpu,arguments,16),16);if(packet[0]!=0x1000||!packet[1]||packet[3])stop("thread-name packet");
 char name[64];unsigned n=0;for(;n<sizeof name;n++){name[n]=*(char *)guest(cpu,packet[1]+n,1);if(!name[n])break;}if(n==sizeof name)stop("thread-name length");
 /* x86 CONTEXT_CONTROL|CONTEXT_INTEGER; other sections are not advertised.
    This runtime's flat user selectors are CS=1b, SS=23. Continuation must
    retain the original registers and guest return point. */
 uint32_t context[179]={0},record[20]={0},return_pc;memcpy(&return_pc,guest(cpu,cpu->_esp,4),4);
 context[0]=0x10003;context[39]=cpu->_edi;context[40]=cpu->_esi;context[41]=cpu->_ebx;context[42]=cpu->_edx;context[43]=cpu->_ecx;context[44]=cpu->_eax;
 context[45]=cpu->_ebp;context[46]=return_pc;context[47]=0x1b;context[48]=cpu->_eflags;context[49]=cpu->_esp+20;context[50]=0x23;
 record[0]=code;record[3]=return_pc;record[4]=count;memcpy(record+5,packet,16);
 if(cpu->_esp<1024||!stack(cpu,cpu->_esp-1024,1024))stop("dispatch stack");
 uint32_t scratch=(cpu->_esp-1024)&~15u,ctx=scratch+128,rec=scratch+848;memcpy(guest(cpu,ctx,sizeof context),context,sizeof context);memcpy(guest(cpu,rec,sizeof record),record,sizeof record);
 uint32_t frame=x86_read_fs_dword(0),initial_frame=frame;
 for(unsigned depth=0;frame!=UINT32_MAX&&depth<64;depth++){
  if(!stack(cpu,frame,8)||frame<cpu->_esp)stop("invalid SEH frame");uint32_t entry[2];memcpy(entry,guest(cpu,frame,8),8);
  if(!entry[1]||(entry[0]!=UINT32_MAX&&entry[0]<=frame))stop("SEH chain");
  uint32_t args[]={rec,frame,ctx,0};uint32_t disposition=tribes_call_guest_cdecl(cpu,entry[1],4,args,scratch);
  if(x86_read_fs_dword(0)!=initial_frame)stop("handler changed chain");
  if(!disposition){uint32_t observed[179];memcpy(observed,guest(cpu,ctx,sizeof observed),sizeof observed);if(memcmp(observed,context,sizeof context))stop("handler changed context");fprintf(stderr,"TribesPad: original SEH continued thread-name notification id=%u name=%s handler=0x%08x\n",packet[2],name,entry[1]);return;}
  if(disposition!=1)stop("unwind/nested disposition");frame=entry[0];
 }
 stop("unhandled notification");
}
