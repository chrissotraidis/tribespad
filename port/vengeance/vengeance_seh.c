/* Structured exception handling for Tribes: Vengeance: RaiseException and
 * RtlUnwind over the guest's fs:[0] chain of registrations, in the order
 * Windows XP's RtlDispatchException and RtlUnwind run them on x86. MSVC's C++
 * throw (_CxxThrowException, code 0xE06D7363) and __try/__except both use it.
 * Adapted from the pinned HaloPad runtime (ref/halopad/port/runtime/halopad_seh.c).
 *
 * The exception record and context go on the guest stack below the raiser,
 * and each handler is called cdecl handler(record, frame, context, dispatcher
 * context) with its own host return address (0xfffff010, vengeance_handler_return).
 * A handler that accepts the exception unwinds (RtlUnwind) and jumps to the
 * __except block or catch continuation itself, so it never returns here: the
 * guest carries on inside this host frame. When the guest later returns to
 * its top-level host entry (vg_enter), the native frames left behind are
 * discarded with longjmp back to that entry, the CPU state intact.
 * Nested exceptions during dispatch, collided unwinds, handlers that move the
 * context and unhandled exceptions stop with the exception code. */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../llasm-support/llasm_cpu.h"
#include "../core/guest_teb.h"

extern void tribes_enter(_cpu *,uint32_t);
extern void (*tribes_lookup(uint32_t address))(_cpu *);
#define HANDLER_RETURN 0xfffff010u
#define END_OF_CHAIN 0xffffffffu
#define NONCONTINUABLE 1u
#define UNWINDING 2u
#define EXIT_UNWIND 4u
#define STATUS_UNWIND 0xc0000027u
#define REC_SIZE 0x50u
#define CTX_SIZE 0x2ccu
enum {CTX_FLAGS=0x00,CTX_EDI=0x9c,CTX_ESI=0xa0,CTX_EBX=0xa4,CTX_EDX=0xa8,CTX_ECX=0xac,CTX_EAX=0xb0,
      CTX_EBP=0xb4,CTX_EIP=0xb8,CTX_CS=0xbc,CTX_EFLAGS=0xc0,CTX_ESP=0xc4,CTX_SS=0xc8};

static _Thread_local jmp_buf *outer;
static _Thread_local _cpu *outer_cpu;
static _Thread_local int handler_returned;
static int tracing(void){static int v=-1;if(v<0)v=getenv("VENGEANCE_TRACE_SEH")!=NULL;return v;}
static uint8_t *G(_cpu *c,uint32_t a){return (uint8_t *)(uintptr_t)(c->_pointer_offset+a);}
static uint32_t rd32(_cpu *c,uint32_t a){uint32_t v;memcpy(&v,G(c,a),4);return v;}
static void wr32(_cpu *c,uint32_t a,uint32_t v){memcpy(G(c,a),&v,4);}
static _Noreturn void unsupported(const char *service,const char *why,uint32_t code){
 printf("{\"diagnostic_stop\":true,\"kind\":\"seh\",\"name\":\"%s: %s\",\"index\":%u}\n",service,why,code);
 fflush(stdout);_Exit(73);
}

void vg_handler_returned(void){handler_returned=1;}

/* A top-level host entry: stdcall arguments, then the guest at va. Returns eax. */
uint32_t vg_enter(_cpu *cpu,uint32_t va,uint32_t count,const uint32_t *arguments){
 cpu->_esp-=4*count;if(count)memcpy(G(cpu,cpu->_esp),arguments,4*count);
 jmp_buf here,*previous=outer;_cpu *previous_cpu=outer_cpu;outer=&here;outer_cpu=cpu;
 if(!setjmp(here))tribes_enter(cpu,va);
 outer=previous;outer_cpu=previous_cpu;
 return cpu->_eax;
}

typedef struct {uint32_t rec,ctx,dispatch,handler_esp;} Area;
static Area area_below(_cpu *c,uint32_t sp){
 Area a;a.dispatch=(sp-0x40-16)&~15u;a.ctx=(a.dispatch-CTX_SIZE)&~15u;a.rec=(a.ctx-REC_SIZE)&~15u;a.handler_esp=a.rec-0x20;
 memset(G(c,a.ctx),0,CTX_SIZE);memset(G(c,a.rec),0,REC_SIZE);wr32(c,a.dispatch,0);return a;
}
/* CONTEXT_FULL at the service's return: registers as the raiser left them,
   esp after the stdcall arguments. Segments of a Windows XP user thread. */
static void capture(_cpu *c,uint32_t ctx,uint32_t nargs){
 wr32(c,ctx+CTX_FLAGS,0x10007u);
 wr32(c,ctx+CTX_EDI,c->_edi);wr32(c,ctx+CTX_ESI,c->_esi);wr32(c,ctx+CTX_EBX,c->_ebx);
 wr32(c,ctx+CTX_EDX,c->_edx);wr32(c,ctx+CTX_ECX,c->_ecx);wr32(c,ctx+CTX_EAX,c->_eax);
 wr32(c,ctx+CTX_EBP,c->_ebp);wr32(c,ctx+CTX_EIP,rd32(c,c->_esp));wr32(c,ctx+CTX_CS,0x1b);
 wr32(c,ctx+CTX_EFLAGS,c->_eflags);wr32(c,ctx+CTX_ESP,c->_esp+4+4*nargs);wr32(c,ctx+CTX_SS,0x23);
 wr32(c,ctx+0x8c,0);wr32(c,ctx+0x90,0x3b);wr32(c,ctx+0x94,0x23);wr32(c,ctx+0x98,0x23);
}
/* RtlpGetStackLimits: a registration lies on this thread's stack, 4-byte aligned. */
static int frame_valid(uint32_t frame){
 uint32_t top=x86_read_fs_dword(4),limit=x86_read_fs_dword(8);
 return !(frame&3)&&frame>=limit&&frame+8<=top;
}
static uint32_t call_handler(_cpu *c,const Area *a,uint32_t frame){
 uint32_t handler=rd32(c,frame+4),sp=a->handler_esp-16;
 uint32_t args[4]={a->rec,frame,a->ctx,a->dispatch};memcpy(G(c,sp),args,16);
 sp-=4;wr32(c,sp,HANDLER_RETURN);c->_esp=sp;
 if(tracing())fprintf(stderr,"vengeance SEH: code 0x%08x flags 0x%x: handler 0x%08x of frame 0x%08x\n",rd32(c,a->rec),rd32(c,a->rec+4),handler,frame);
 handler_returned=0;
 tribes_lookup(handler)(c);
 if(!handler_returned){
  /* The handler accepted the exception and its continuation has since
     returned to the top-level entry: discard this host frame. */
  if(!outer||outer_cpu!=c)unsupported("SEH","guest returned past a handler outside a top-level entry",rd32(c,a->rec));
  if(tracing())fprintf(stderr,"vengeance SEH: continuation returned to its entry; leaving the handler frames\n");
  longjmp(*outer,1);
 }
 handler_returned=0;
 uint32_t d=c->_eax;
 if(tracing())fprintf(stderr,"vengeance SEH:   handler 0x%08x returned %u\n",handler,d);
 return d;
}
typedef struct {uint32_t sp,bp,bx,si,di;} Saved;
static Saved save(_cpu *c){return (Saved){c->_esp,c->_ebp,c->_ebx,c->_esi,c->_edi};}
static void restore(_cpu *c,Saved s){c->_esp=s.sp;c->_ebp=s.bp;c->_ebx=s.bx;c->_esi=s.si;c->_edi=s.di;}

/* The thrown C++ type's name, for the unhandled-exception report: ThrowInfo,
   CatchableTypeArray, first CatchableType, its TypeDescriptor name. */
static void cxx_type(_cpu *c,uint32_t rec,char *out,size_t n){
 out[0]=0;if(rd32(c,rec)!=0xe06d7363u||rd32(c,rec+0x10)<3)return;
 uint32_t info=rd32(c,rec+0x14+8);if(!info)return;
 uint32_t array=rd32(c,info+12);if(!array||!rd32(c,array))return;
 uint32_t type=rd32(c,rd32(c,array+4)+4);if(!type)return;
 for(size_t i=0;i+1<n;i++){out[i]=(char)*G(c,type+8+(uint32_t)i);out[i+1]=0;if(!out[i])break;}
}

/* void RaiseException(DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR *args) */
void vg_raise_exception(_cpu *c,uint32_t code,uint32_t flags,uint32_t nargs,uint32_t args){
 Saved s=save(c);Area a=area_below(c,s.sp);
 if(nargs>15)nargs=15;
 wr32(c,a.rec+0x00,code);wr32(c,a.rec+0x04,flags&NONCONTINUABLE);wr32(c,a.rec+0x08,0);
 wr32(c,a.rec+0x0c,rd32(c,s.sp));wr32(c,a.rec+0x10,args?nargs:0);
 for(uint32_t i=0;args&&i<nargs;i++)wr32(c,a.rec+0x14+4*i,rd32(c,args+4*i));
 capture(c,a.ctx,4);
 uint32_t eip0=rd32(c,a.ctx+CTX_EIP),esp0=rd32(c,a.ctx+CTX_ESP);
 if(tracing()){char type[96];cxx_type(c,a.rec,type,sizeof type);fprintf(stderr,"vengeance SEH: raise 0x%08x %s\n",code,type);}
 for(uint32_t frame=x86_read_fs_dword(0);frame!=END_OF_CHAIN;frame=rd32(c,frame)){
  if(!frame_valid(frame))unsupported("RaiseException","registration outside the stack",code);
  uint32_t d=call_handler(c,&a,frame);restore(c,s);
  if(d==1)continue; /* ExceptionContinueSearch */
  if(d==0){ /* ExceptionContinueExecution */
   if(flags&NONCONTINUABLE)unsupported("RaiseException","continuing a non-continuable exception",code);
   if(rd32(c,a.ctx+CTX_EIP)!=eip0||rd32(c,a.ctx+CTX_ESP)!=esp0)unsupported("RaiseException","a handler that moves the context",code);
   return;
  }
  unsupported("RaiseException","nested or collided dispatch",code);
 }
 char type[96];cxx_type(c,a.rec,type,sizeof type);
 printf("{\"diagnostic_stop\":true,\"kind\":\"unhandled_exception\",\"index\":%u,\"name\":\"%s\",\"return_pc\":%u}\n",code,type,eip0);
 fflush(stdout);_Exit(74);
}

/* RtlUnwind(target frame, target ip, record, return value): each registration's
   handler with EXCEPTION_UNWINDING from the head down to the target, unlinking
   each. x86 Windows ignores the target ip and returns with eax = return value. */
uint32_t vg_rtl_unwind(_cpu *c,uint32_t target,uint32_t ip,uint32_t record,uint32_t value){
 (void)ip;Saved s=save(c);Area a=area_below(c,s.sp);
 if(record)memcpy(G(c,a.rec),G(c,record),REC_SIZE);else{wr32(c,a.rec+0x00,STATUS_UNWIND);wr32(c,a.rec+0x0c,rd32(c,s.sp));}
 wr32(c,a.rec+0x04,rd32(c,a.rec+0x04)|UNWINDING|(target?0:EXIT_UNWIND));
 capture(c,a.ctx,4);
 uint32_t frame=x86_read_fs_dword(0);
 while(frame!=END_OF_CHAIN&&frame!=target){
  if(target&&target<frame)unsupported("RtlUnwind","target frame below a registration",target);
  if(!frame_valid(frame))unsupported("RtlUnwind","registration outside the stack",frame);
  uint32_t d=call_handler(c,&a,frame);restore(c,s);
  if(d!=1)unsupported("RtlUnwind","handler disposition while unwinding",d);
  uint32_t next=rd32(c,frame);x86_write_fs_dword(0,next);frame=next;
 }
 if(frame==END_OF_CHAIN&&target&&target!=END_OF_CHAIN)unsupported("RtlUnwind","target frame not on the chain",target);
 return value;
}
