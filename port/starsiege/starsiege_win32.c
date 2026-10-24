/* Windows services Starsiege: Tribes 1.41 imports that Tribes 2 never reached.
 * Bound through config/starsiege/bindings.json (Tribes 2's
 * port/llasm-runtime/win32-bindings.json and its receipts stay unchanged).
 * Guest pointers go through the same access brackets as port/core services,
 * so checked builds verify them; unchecked builds pass straight through. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/guest_memory_checks.h"

extern uint32_t tribes_set_last_error(uint32_t value);

static uint32_t *guest_word(uint64_t base,uint32_t address){
 if(!address||(address&3)){fprintf(stderr,"TribesPad: unsupported Interlocked address 0x%08x\n",address);abort();}
 return (uint32_t *)(uintptr_t)(base+address);
}
/* LONG InterlockedIncrement(LONG volatile *); returns the new value. */
uint32_t ss_interlocked_increment(uint64_t base,uint32_t address){
 uint32_t *p=guest_word(base,address),token=tribes_memory_access_begin(p,4,3);
 uint32_t value=__atomic_add_fetch(p,1,__ATOMIC_SEQ_CST);tribes_memory_access_end(token);return value;
}
uint32_t ss_interlocked_decrement(uint64_t base,uint32_t address){
 uint32_t *p=guest_word(base,address),token=tribes_memory_access_begin(p,4,3);
 uint32_t value=__atomic_sub_fetch(p,1,__ATOMIC_SEQ_CST);tribes_memory_access_end(token);return value;
}
/* LONG InterlockedExchange(LONG volatile *, LONG); returns the old value. */
uint32_t ss_interlocked_exchange(uint64_t base,uint32_t address,uint32_t value){
 uint32_t *p=guest_word(base,address),token=tribes_memory_access_begin(p,4,3);
 uint32_t old=__atomic_exchange_n(p,value,__ATOMIC_SEQ_CST);tribes_memory_access_end(token);return old;
}
/* No debugger is ever attached to the guest. */
uint32_t ss_is_debugger_present(void){return 0;}

/* BOOL Get/SetDeviceGammaRamp: the guest display has no hardware gamma
   ramp. Windows allows failure here, and games fall back to no gamma. */
uint32_t ss_device_gamma_ramp(uint32_t dc,uint32_t ramp){(void)dc;(void)ramp;return 0;}

/* ATOM RegisterClassExA(const WNDCLASSEXA *). WNDCLASSA is WNDCLASSEXA's
   fields 4..43 in the same order, so this checks cbSize and registers through
   TribesPad's RegisterClassA service. The small icon (hIconSm) is a window
   decoration the guest window never shows, so it is not kept. */
extern uint32_t tribes_register_class(uint64_t base,uint32_t descriptor);
uint32_t ss_register_class_ex_a(uint64_t base,uint32_t descriptor){
 if(!descriptor){tribes_set_last_error(87);return 0;}
 uint32_t size;const void *p=(const void *)(uintptr_t)(base+descriptor);
 uint32_t token=tribes_memory_access_begin(p,48,1);memcpy(&size,p,4);tribes_memory_access_end(token);
 if(size!=48){tribes_set_last_error(87);return 0;}
 return tribes_register_class(base,descriptor+4);
}

/* HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR).
   TribesPad runs one guest process, so a name is new the first time it is
   created (last error 0, which the game reads as "not already running"). A
   second creation of the same name in this process stays a strict gate.
   Security attributes are accepted only without a descriptor: there are no
   child processes to inherit handles. */
extern uint32_t tribes_create_semaphore(uint64_t base,uint32_t attributes,uint32_t initial,uint32_t maximum,uint32_t name);
static char semaphore_names[16][261];
static unsigned semaphore_count;
uint32_t ss_create_semaphore(uint64_t base,uint32_t attributes,uint32_t initial,uint32_t maximum,uint32_t name){
 if(attributes){
  uint32_t *sa=(uint32_t *)(uintptr_t)(base+attributes),token=tribes_memory_access_begin(sa,12,1);
  uint32_t length=sa[0],descriptor=sa[1];tribes_memory_access_end(token);
  if(length<12||descriptor){fprintf(stderr,"TribesPad: semaphore security descriptors are not implemented\n");abort();}
 }
 if(name){
  const char *text=(const char *)(uintptr_t)(base+name);char copy[261];unsigned n=0;
  uint32_t token=tribes_memory_access_begin(text,1,1);tribes_memory_access_end(token);
  for(;n<260&&text[n];n++)copy[n]=text[n];copy[n]=0;
  if(n==260){fprintf(stderr,"TribesPad: semaphore name too long\n");abort();}
  for(unsigned i=0;i<semaphore_count;i++)if(!strcmp(semaphore_names[i],copy)){fprintf(stderr,"TribesPad: second creation of named semaphore %s is not implemented\n",copy);abort();}
  if(semaphore_count==16){fprintf(stderr,"TribesPad: too many named semaphores\n");abort();}
  strcpy(semaphore_names[semaphore_count++],copy);fprintf(stderr,"TribesPad: named semaphore %s (process-local)\n",copy);
 }
 uint32_t handle=tribes_create_semaphore(base,0,initial,maximum,0);
 if(handle)tribes_set_last_error(0);
 return handle;
}



