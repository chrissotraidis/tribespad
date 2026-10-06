/* SDL 3.2.18 TLS backed by real native SDL storage. Guest IDs are opaque32-bit
   tokens; native IDs, values and destructor functions stay outside guest RAM. */
#include "guest_sdl_tls.h"
#include "guest_memory_checks.h"
#include "guest_heap.h"
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Slot { uint64_t base;uint32_t token;SDL_TLSID native;struct Slot *next; } Slot;
typedef struct Value { uint64_t base;uint32_t value,destructor;struct Value *next; } Value;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static Slot *slots;
static uint32_t next_token=0x50000001;
static _Thread_local _cpu *cleanup_cpu;
static _Thread_local Value *retired;
static _Noreturn void stop(void){fputs("TribesPad: invalid SDL TLS ownership/lifecycle\n",stderr);abort();}
static Slot *slot(_cpu *cpu,uint32_t address,int create) {
 if(!cpu||!cpu->_pointer_offset||!address||(address&3)||address>UINT32_MAX-3||cpu->_pointer_offset>UINTPTR_MAX-address-3)stop();
 void *p=(void *)(uintptr_t)(cpu->_pointer_offset+address);uint32_t token;
 pthread_mutex_lock(&gate);uint32_t access=tribes_memory_transfer_begin(&token,p,4);memcpy(&token,p,4);tribes_memory_access_end(access);
 Slot *s=NULL;
 if(token){for(s=slots;s;s=s->next)if(s->token==token)break;if(!s||s->base!=cpu->_pointer_offset)stop();}
 else if(create){
  if(next_token>=0x60000000)stop();s=calloc(1,sizeof *s);
  if(s){s->base=cpu->_pointer_offset;s->token=next_token++;s->next=slots;slots=s;
   access=tribes_memory_transfer_begin(p,&s->token,4);memcpy(p,&s->token,4);tribes_memory_access_end(access);}
 }
 pthread_mutex_unlock(&gate);return s;
}
static void destroy_value(void *opaque) {
 Value *v=opaque;if(!v||!cleanup_cpu||cleanup_cpu->_pointer_offset!=v->base)stop();
 if(v->destructor){
  if(cleanup_cpu->_esp<128)stop();uint32_t error=tribes_get_last_error();
  tribes_call_guest_cdecl(cleanup_cpu,v->destructor,1,&v->value,cleanup_cpu->_esp-128);
  tribes_set_last_error(error);
 }
 /* Keep the snapshot until all destructors finish: SDL_GetTLS remains valid
    during later destructors, even if an earlier guest destructor freed data. */
 v->next=retired;retired=v;
}
void tribes_sdl_tls_cleanup_begin(_cpu *cpu){if(!cpu||cleanup_cpu||retired)stop();cleanup_cpu=cpu;}
void tribes_sdl_tls_cleanup_end(void){
 if(!cleanup_cpu)stop();while(retired){Value *v=retired;retired=v->next;free(v);}cleanup_cpu=NULL;
}
void tribes_sdl_cleanup_tls(_cpu *cpu){
 uint32_t error=tribes_get_last_error();tribes_sdl_tls_cleanup_begin(cpu);SDL_CleanupTLS();tribes_sdl_tls_cleanup_end();tribes_set_last_error(error);
}
uint32_t tribes_sdl_get_tls(_cpu *cpu,uint32_t address){
 uint32_t error=tribes_get_last_error(),result=0;
 if(!address){(void)SDL_GetTLS(NULL);tribes_set_last_error(error);return 0;}
 Slot *s=slot(cpu,address,0);Value *v=s?SDL_GetTLS(&s->native):NULL;
 if(v){if(v->base!=cpu->_pointer_offset)stop();result=v->value;}
 tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_set_tls(_cpu *cpu,uint32_t address,uint32_t value,uint32_t destructor){
 uint32_t error=tribes_get_last_error();
 if(cleanup_cpu)stop(); /* The pinned native cleanup loop cannot safely grow or replace its own storage. */
 if(!address){(void)SDL_SetTLS(NULL,NULL,NULL);tribes_set_last_error(error);return 0;}
 Slot *s=slot(cpu,address,1);
 if(!s||!tribes_guest_finalizer_register(tribes_sdl_cleanup_tls)){SDL_OutOfMemory();tribes_set_last_error(error);return 0;}
 Value *v=NULL;
 if(value||destructor){v=calloc(1,sizeof *v);if(!v){SDL_OutOfMemory();tribes_set_last_error(error);return 0;}v->base=cpu->_pointer_offset;v->value=value;v->destructor=destructor;}
 Value *old=SDL_GetTLS(&s->native);
 uint32_t result=SDL_SetTLS(&s->native,v,v?destroy_value:NULL);
 if(result)free(old);else free(v); /* Replacement never invokes the old destructor. */
 tribes_set_last_error(error);return result;
}
