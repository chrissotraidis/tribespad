/* Starsiege's DirectSound buffers. TribesPad's provider (port/core/
 * guest_dsound.c, shared with Tribes 2) implements the global-focus profile
 * Tribes 2 uses and stops on anything else. Starsiege also creates ordinary
 * focus-dependent secondary buffers (no DSBCAPS_GLOBALFOCUS), which Windows
 * plays while the game window has focus. TribesPad's game window is the only
 * window and has focus whenever the game is on screen (the app pauses audio in
 * the background), so they play the same as global-focus buffers: mark the
 * request global-focus for the provider and restore the caller's descriptor.
 * 3D buffers (DSBCAPS_CTRL3D) still reach the provider unchanged and stop
 * there, until they have a real implementation. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

extern uint32_t tribes_ds_create_buffer(uint64_t base,uint32_t object,uint32_t desc,uint32_t output,uint32_t outer);
extern uint32_t tribes_dsb_caps(uint64_t base,uint32_t object,uint32_t output);
extern uint32_t tribes_dsb_format(uint64_t base,uint32_t object,uint32_t output,uint32_t capacity,uint32_t written);
extern uint32_t tribes_dsb_lock(uint64_t base,uint32_t object,uint32_t offset,uint32_t bytes,uint32_t p1,uint32_t n1,uint32_t p2,uint32_t n2,uint32_t flags);
extern uint32_t tribes_dsb_unlock(uint64_t base,uint32_t object,uint32_t p1,uint32_t n1,uint32_t p2,uint32_t n2);
extern uint32_t tribes_dsb_volume(uint64_t base,uint32_t object,uint32_t output);
extern uint32_t tribes_dsb_pan(uint64_t base,uint32_t object,uint32_t output);
extern uint32_t tribes_dsb_frequency(uint64_t base,uint32_t object,uint32_t output);
extern uint32_t tribes_dsb_set_volume(uint64_t base,uint32_t object,uint32_t value);
extern uint32_t tribes_dsb_set_pan(uint64_t base,uint32_t object,uint32_t value);
extern uint32_t tribes_dsb_set_frequency(uint64_t base,uint32_t object,uint32_t value);
extern uint32_t tribes_global_alloc(uint64_t base,uint32_t flags,uint32_t bytes);

uint32_t ss_ds_create_buffer(uint64_t base,uint32_t object,uint32_t desc,uint32_t output,uint32_t outer){
 if(!desc)return tribes_ds_create_buffer(base,object,desc,output,outer);
 unsigned char *p=(unsigned char *)(uintptr_t)(base+desc);uint32_t size,flags;memcpy(&size,p,4);memcpy(&flags,p+4,4);
 /* Secondary (not DSBCAPS_PRIMARYBUFFER 1), not 3D (0x10), no focus flag (GLOBALFOCUS 0x8000, STICKYFOCUS 0x4000). */
 if((size!=20&&size!=36)||(flags&0xc011u))return tribes_ds_create_buffer(base,object,desc,output,outer);
 static int told;if(!told){told=1;fprintf(stderr,"TribesPad: Starsiege focus-dependent DirectSound buffers (flags 0x%x) play as global focus\n",flags);}
 uint32_t global=flags|0x8000u;memcpy(p+4,&global,4);
 uint32_t result=tribes_ds_create_buffer(base,object,desc,output,outer);
 memcpy(p+4,&flags,4);return result;
}

/* HRESULT DuplicateSoundBuffer(this, original, LPDIRECTSOUNDBUFFER *out).
   Starsiege plays one loaded sound several times at once through duplicates.
   Windows shares the original's memory; Starsiege never writes a sound after
   loading it, so a buffer with the original's flags, size and format, its
   samples copied, and its volume, pan and frequency, plays the same. The
   descriptor and out-parameters live in one guest scratch block, under a lock. */
uint32_t ss_ds_duplicate(uint64_t base,uint32_t object,uint32_t original,uint32_t output){
 static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;static uint32_t scratch;
 if(!original||!output)return 0x80070057u;
 pthread_mutex_lock(&lock);
 if(!scratch)scratch=tribes_global_alloc(base,0x40,256);
 if(!scratch){pthread_mutex_unlock(&lock);return 0x8007000eu;}
 unsigned char *s=(unsigned char *)(uintptr_t)(base+scratch);memset(s,0,256);
 uint32_t caps=scratch,format=scratch+32,desc=scratch+64,p1=scratch+100,n1=scratch+104,p2=scratch+108,n2=scratch+112,value=scratch+116,copy=scratch+120;
 uint32_t twenty=20;memcpy(s,&twenty,4);
 uint32_t result=tribes_dsb_caps(base,original,caps);
 if(!result)result=tribes_dsb_format(base,original,format,18,0);
 uint32_t flags,bytes;memcpy(&flags,s+4,4);memcpy(&bytes,s+8,4);
 if(!result){uint32_t d[5]={20,flags&~8u,bytes,0,format};memcpy(s+64,d,20);result=ss_ds_create_buffer(base,object,desc,copy,0);}
 uint32_t duplicate=0;if(!result)memcpy(&duplicate,s+120,4);
 if(!result){
  /* Whole-buffer locks: the original's samples straight into the copy. */
  result=tribes_dsb_lock(base,original,0,0,p1,n1,p2,n2,2);
  if(!result){
   uint32_t from,count;memcpy(&from,s+100,4);memcpy(&count,s+104,4);
   uint32_t to_lock=tribes_dsb_lock(base,duplicate,0,0,p1,n1,p2,n2,2);
   if(!to_lock){uint32_t to,space;memcpy(&to,s+100,4);memcpy(&space,s+104,4);
    memcpy((void *)(uintptr_t)(base+to),(void *)(uintptr_t)(base+from),count<space?count:space);
    result=tribes_dsb_unlock(base,duplicate,to,count<space?count:space,0,0);}
   else result=to_lock;
   uint32_t unlocked=tribes_dsb_unlock(base,original,from,0,0,0);if(!result)result=unlocked;
  }
 }
 if(!result){
  uint32_t v;
  if(flags&0x80&&!tribes_dsb_volume(base,original,value)){memcpy(&v,s+116,4);(void)tribes_dsb_set_volume(base,duplicate,v);}
  if(flags&0x40&&!tribes_dsb_pan(base,original,value)){memcpy(&v,s+116,4);(void)tribes_dsb_set_pan(base,duplicate,v);}
  if(flags&0x20&&!tribes_dsb_frequency(base,original,value)){memcpy(&v,s+116,4);(void)tribes_dsb_set_frequency(base,duplicate,v);}
  memcpy((void *)(uintptr_t)(base+output),&duplicate,4);
  static unsigned logged;if(logged<3){logged++;fprintf(stderr,"TribesPad: DirectSound duplicate 0x%08x of 0x%08x (%u bytes, flags 0x%x)\n",duplicate,original,bytes,flags);}
 } else {
  uint32_t zero=0;memcpy((void *)(uintptr_t)(base+output),&zero,4);
  fprintf(stderr,"TribesPad: DirectSound duplicate of 0x%08x failed 0x%08x\n",original,result);
 }
 pthread_mutex_unlock(&lock);return result;
}
