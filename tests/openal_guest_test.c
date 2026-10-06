#include "../port/core/guest_openal.h"
#include "../port/core/guest_callback.h"
#include "../port/core/guest_modules.h"
#include "../port/core/guest_memory_checks.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/efx.h>
#include <AL/alext.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t),tribes_return_to_host(void);
extern void x87_fninit_void(_cpu *);
extern int32_t x87_fstp_float(_cpu *);
extern uint32_t tribes_openal_test_loopback(_cpu *,uint32_t *);
extern void tribes_openal_test_render(_cpu *,uint32_t,float *,unsigned);
static unsigned char *memory;
void *tribes_lookup(uint32_t address){if(address==0xfffff000)return (void *)tribes_return_to_host;void *p=tribes_openal_native_lookup(address);assert(p);return p;}
static uint32_t invoke(_cpu *c,const char *name,unsigned n,const uint32_t *args,int fp){
 strcpy((char *)memory+0x100000,name);uint32_t target=tribes_get_proc_address(c->_pointer_offset,TB_OPENAL_MODULE,0x100000);assert(target);
 _cpu before=*c;c->_esp-=4*n;if(n)memcpy(memory+c->_esp,args,n*4);tribes_enter(c,target);assert(c->_esp==before._esp-4*n);c->_esp+=4*n;
 uint32_t r=c->_eax;before._eax=r;if(!fp)assert(!memcmp(&before,c,sizeof before));else {assert(c->_ecx==before._ecx&&c->_eflags==before._eflags&&c->_pointer_offset==before._pointer_offset);}return r;
}
#define CALL(name,...) invoke(&cpu,#name,sizeof((uint32_t[]){__VA_ARGS__})/4,(uint32_t[]){__VA_ARGS__},0)
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory+0x100000,0x100000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));
 _cpu cpu={0};cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x1f0000;cpu._eflags=0x202;cpu._st_cw=0x37f;cpu.stack_bottom=memory+0x180000;cpu.stack_top=memory+0x200000;
 x87_fninit_void(&cpu);
 assert(tribes_openal_load(cpu._pointer_offset,"openal32.dll")==TB_OPENAL_MODULE);assert(tribes_module_retain_native(cpu._pointer_offset,TB_OPENAL_MODULE));assert(tribes_openal_load(cpu._pointer_offset,"soft_oal.dll")==TB_OPENAL_MODULE);assert(!tribes_openal_load(cpu._pointer_offset,"missing.dll"));
 int offline=argc>2&&!strcmp(argv[2],"offline");uint32_t ctx=0;uint32_t d=offline?tribes_openal_test_loopback(&cpu,&ctx):CALL(alcOpenDevice,0);assert(d);
 /* Exact original attributes at return10058442, with loopback sample format
    added only for the offline fixture. The production list requests 4 sends. */
 uint32_t attrs[]={ALC_FREQUENCY,22050,ALC_REFRESH,32,ALC_MONO_SOURCES,63,ALC_STEREO_SOURCES,1,ALC_MAX_AUXILIARY_SENDS,4,ALC_FORMAT_CHANNELS_SOFT,ALC_STEREO_SOFT,ALC_FORMAT_TYPE_SOFT,ALC_FLOAT_SOFT,0};
 if(!offline)attrs[10]=0;else CALL(alcDestroyContext,ctx);
 memcpy(memory+0x102000,attrs,sizeof attrs);
 if(argc>1&&!strcmp(argv[1],"unreadable-attributes"))assert(tribes_memory_permissions_set(0x102000,4096,0));
 ctx=CALL(alcCreateContext,d,0x102000);assert(ctx&&ctx!=d);CALL(alcGetIntegerv,d,ALC_MAX_AUXILIARY_SENDS,1,0x101100);assert(*(uint32_t *)(memory+0x101100)==4);assert(CALL(alcMakeContextCurrent,ctx));assert(CALL(alcGetCurrentContext)==ctx);assert(CALL(alcGetContextsDevice,ctx)==d);
 assert(CALL(alcGetError,d)==ALC_NO_ERROR);
 uint32_t renderer=CALL(alGetString,AL_RENDERER);assert(renderer&&!strcmp((char *)memory+renderer,"OpenAL Soft"));assert(CALL(alGetString,AL_RENDERER)==renderer);
 strcpy((char *)memory+0x100100,"ALC_SOFT_events");assert(!CALL(alcIsExtensionPresent,d,0x100100));assert(!strcmp((char *)memory+CALL(alcGetString,d,ALC_EXTENSIONS),"ALC_EXT_EFX"));
 strcpy((char *)memory+0x100100,"ALC_EXT_EFX");assert(CALL(alcIsExtensionPresent,d,0x100100));
 strcpy((char *)memory+0x100100,"alBufferData");assert(CALL(alGetProcAddress,0x100100));strcpy((char *)memory+0x100100,"alBufferCallbackSOFT");assert(!CALL(alGetProcAddress,0x100100));
 CALL(alDopplerFactor,0x40000000);uint32_t pname=AL_DOPPLER_FACTOR;invoke(&cpu,"alGetFloat",1,&pname,1);assert((uint32_t)x87_fstp_float(&cpu)==0x40000000);invoke(&cpu,"alGetDouble",1,&pname,1);assert((uint32_t)x87_fstp_float(&cpu)==0x40000000);
 if(argc>1&&!strcmp(argv[1],"readonly"))assert(tribes_memory_permissions_set(0x101000,4096,1));
 CALL(alGenBuffers,1,0x101000);uint32_t b=*(uint32_t *)(memory+0x101000);assert(b&&CALL(alIsBuffer,b));
 int16_t *pcm=(void *)(memory+0x110000);for(unsigned i=0;i<4096;i++)pcm[i]=(int16_t)(1000*sin(i*6.283185307179586/64));
 if(argc>1&&!strcmp(argv[1],"unreadable-pcm"))assert(tribes_memory_permissions_set(0x110000,8192,0));
 CALL(alBufferData,b,AL_FORMAT_MONO16,0x110000,8192,48000);memset(pcm,0,8192);CALL(alGetBufferi,b,AL_SIZE,0x101100);assert(*(uint32_t *)(memory+0x101100)==8192);
 CALL(alGenSources,1,0x101004);uint32_t s=*(uint32_t *)(memory+0x101004);assert(s&&CALL(alIsSource,s));CALL(alSourcei,s,AL_BUFFER,b);CALL(alSourcei,s,AL_LOOPING,AL_TRUE);CALL(alSourcef,s,AL_GAIN,0x3ca3d70a);CALL(alSource3f,s,AL_POSITION,0xbf800000,0,0xbf800000);CALL(alGetSource3f,s,AL_POSITION,0x101100,0x101104,0x101108);assert(*(uint32_t *)(memory+0x101100)==0xbf800000&&*(uint32_t *)(memory+0x101108)==0xbf800000);
 CALL(alSourcePlay,s);uint32_t offset=0;for(unsigned i=0;i<50&&!offset;i++){if(offline){float samples[1024];tribes_openal_test_render(&cpu,d,samples,512);}else usleep(10000);CALL(alGetSourceiv,s,AL_SAMPLE_OFFSET,0x101100);offset=*(uint32_t *)(memory+0x101100);}assert(offset);CALL(alGetSourceiv,s,AL_SOURCE_STATE,0x101100);assert(*(uint32_t *)(memory+0x101100)==AL_PLAYING);assert(CALL(alGetError)==AL_NO_ERROR);
 if(offline){float samples[4096];double left=0,right=0;tribes_openal_test_render(&cpu,d,samples,2048);for(unsigned i=0;i<2048;i++){left+=samples[2*i]*samples[2*i];right+=samples[2*i+1]*samples[2*i+1];}assert(left>right*2&&left>0);printf("Guest positional PCM %.8f / %.8f\n",left,right);}
 CALL(alSourceStop,s);CALL(alSourcei,s,AL_LOOPING,AL_FALSE);CALL(alSourcei,s,AL_BUFFER,0);CALL(alSourceQueueBuffers,s,1,0x101000);CALL(alSourcePlay,s);CALL(alSourceStop,s);CALL(alSourceUnqueueBuffers,s,1,0x101100);assert(*(uint32_t *)(memory+0x101100)==b);assert(CALL(alGetError)==AL_NO_ERROR);
 /* EFX: exercise real native objects, parameter widths and audible reverb
    routing through the same original-width guest ABI as the game. */
 strcpy((char *)memory+0x100100,"alGenAuxiliaryEffectSlots");assert(CALL(alGetProcAddress,0x100100));assert(CALL(alcGetProcAddress,d,0x100100));
 if(argc>1&&!strcmp(argv[1],"readonly-efx"))assert(tribes_memory_permissions_set(0x103000,4096,1));
 CALL(alGenEffects,1,0x103000);CALL(alGenFilters,1,0x103004);CALL(alGenAuxiliaryEffectSlots,1,0x103008);
 uint32_t effect=*(uint32_t *)(memory+0x103000),filter=*(uint32_t *)(memory+0x103004),slot=*(uint32_t *)(memory+0x103008);
 assert(CALL(alIsEffect,effect)&&CALL(alIsFilter,filter)&&CALL(alIsAuxiliaryEffectSlot,slot));
 CALL(alEffecti,effect,AL_EFFECT_TYPE,AL_EFFECT_EAXREVERB);
 CALL(alGetEffecti,effect,AL_EFFECT_TYPE,0x103100);assert(*(uint32_t *)(memory+0x103100)==AL_EFFECT_EAXREVERB);
 CALL(alEffectf,effect,AL_EAXREVERB_GAIN,0x3f000000);CALL(alGetEffectf,effect,AL_EAXREVERB_GAIN,0x103100);assert(*(uint32_t *)(memory+0x103100)==0x3f000000);
 *(uint32_t *)(memory+0x103100)=1;CALL(alEffectiv,effect,AL_EAXREVERB_DECAY_HFLIMIT,0x103100);CALL(alGetEffectiv,effect,AL_EAXREVERB_DECAY_HFLIMIT,0x103104);assert(*(uint32_t *)(memory+0x103104)==1);
 float pan[]={0.25f,0.5f,-0.25f};memcpy(memory+0x104ff8,pan,sizeof pan);
 if(argc>1&&!strcmp(argv[1],"unreadable-efx-vector"))assert(tribes_memory_permissions_set(0x105000,4096,0));
 CALL(alEffectfv,effect,AL_EAXREVERB_REFLECTIONS_PAN,0x104ff8);
 if(argc>1&&!strcmp(argv[1],"readonly-efx-vector"))assert(tribes_memory_permissions_set(0x105000,4096,1));
 CALL(alGetEffectfv,effect,AL_EAXREVERB_REFLECTIONS_PAN,0x104ff8);assert(!memcmp(memory+0x104ff8,pan,sizeof pan));
 memset(memory+0x104ff8,0,sizeof pan);CALL(alEffectfv,effect,AL_EAXREVERB_REFLECTIONS_PAN,0x104ff8);CALL(alEffectfv,effect,AL_EAXREVERB_LATE_REVERB_PAN,0x104ff8);
 /* Same numeric parameter is scalar on standard reverb: no over-read. */
 CALL(alEffecti,effect,AL_EFFECT_TYPE,AL_EFFECT_REVERB);*(float *)(memory+0x104ffc)=0.994f;assert(tribes_memory_permissions_set(0x105000,4096,0));CALL(alEffectfv,effect,AL_REVERB_AIR_ABSORPTION_GAINHF,0x104ffc);CALL(alGetEffectfv,effect,AL_REVERB_AIR_ABSORPTION_GAINHF,0x104ffc);assert(tribes_memory_permissions_set(0x105000,4096,3));
 CALL(alEffecti,effect,AL_EFFECT_TYPE,AL_EFFECT_EAXREVERB);
 CALL(alFilteri,filter,AL_FILTER_TYPE,AL_FILTER_LOWPASS);CALL(alGetFilteri,filter,AL_FILTER_TYPE,0x103100);assert(*(uint32_t *)(memory+0x103100)==AL_FILTER_LOWPASS);
 *(uint32_t *)(memory+0x103100)=AL_FILTER_LOWPASS;CALL(alFilteriv,filter,AL_FILTER_TYPE,0x103100);CALL(alGetFilteriv,filter,AL_FILTER_TYPE,0x103104);assert(*(uint32_t *)(memory+0x103104)==AL_FILTER_LOWPASS);
 CALL(alFilterf,filter,AL_LOWPASS_GAIN,0);CALL(alGetFilterf,filter,AL_LOWPASS_GAIN,0x103100);assert(!*(uint32_t *)(memory+0x103100));
 *(uint32_t *)(memory+0x103100)=0x3f800000;CALL(alFilterfv,filter,AL_LOWPASS_GAINHF,0x103100);CALL(alGetFilterfv,filter,AL_LOWPASS_GAINHF,0x103104);assert(*(uint32_t *)(memory+0x103104)==0x3f800000);
 CALL(alAuxiliaryEffectSloti,slot,AL_EFFECTSLOT_EFFECT,effect);CALL(alGetAuxiliaryEffectSloti,slot,AL_EFFECTSLOT_EFFECT,0x103100);assert(*(uint32_t *)(memory+0x103100)==effect);
 *(uint32_t *)(memory+0x103100)=1;CALL(alAuxiliaryEffectSlotiv,slot,AL_EFFECTSLOT_AUXILIARY_SEND_AUTO,0x103100);CALL(alGetAuxiliaryEffectSlotiv,slot,AL_EFFECTSLOT_AUXILIARY_SEND_AUTO,0x103104);assert(*(uint32_t *)(memory+0x103104)==1);
 CALL(alAuxiliaryEffectSlotf,slot,AL_EFFECTSLOT_GAIN,0x3f000000);CALL(alGetAuxiliaryEffectSlotf,slot,AL_EFFECTSLOT_GAIN,0x103100);assert(*(uint32_t *)(memory+0x103100)==0x3f000000);
 *(uint32_t *)(memory+0x103100)=0x3f800000;CALL(alAuxiliaryEffectSlotfv,slot,AL_EFFECTSLOT_GAIN,0x103100);CALL(alGetAuxiliaryEffectSlotfv,slot,AL_EFFECTSLOT_GAIN,0x103104);assert(*(uint32_t *)(memory+0x103104)==0x3f800000);
 CALL(alSourcei,s,AL_BUFFER,b);CALL(alSourcei,s,AL_DIRECT_FILTER,filter);
 assert(CALL(alGetError)==AL_NO_ERROR);
 if(offline){float samples[16384];double dry=0,wet=0;CALL(alSourcePlay,s);tribes_openal_test_render(&cpu,d,samples,8192);for(unsigned i=8192;i<16384;i++)dry+=(double)samples[i]*samples[i];
  uint32_t send[]={slot,0,AL_FILTER_NULL};memcpy(memory+0x103100,send,sizeof send);CALL(alSourceiv,s,AL_AUXILIARY_SEND_FILTER,0x103100);CALL(alSourceRewind,s);CALL(alSourcePlay,s);tribes_openal_test_render(&cpu,d,samples,8192);for(unsigned i=8192;i<16384;i++)wet+=(double)samples[i]*samples[i];assert(wet>dry+1e-9);printf("Guest EFX reverb tail dry %.12f wet %.12f\n",dry,wet);
 }else{CALL(alSource3i,s,AL_AUXILIARY_SEND_FILTER,slot,0,AL_FILTER_NULL);CALL(alSourcePlay,s);usleep(100000);}
 assert(CALL(alGetError)==AL_NO_ERROR);CALL(alSourceStop,s);CALL(alSource3i,s,AL_AUXILIARY_SEND_FILTER,0,0,0);CALL(alSourcei,s,AL_DIRECT_FILTER,0);
 CALL(alAuxiliaryEffectSloti,slot,AL_EFFECTSLOT_EFFECT,AL_EFFECT_NULL);CALL(alDeleteAuxiliaryEffectSlots,1,0x103008);CALL(alDeleteEffects,1,0x103000);CALL(alDeleteFilters,1,0x103004);
 assert(!CALL(alIsEffect,effect)&&!CALL(alIsFilter,filter)&&!CALL(alIsAuxiliaryEffectSlot,slot));assert(CALL(alGetError)==AL_NO_ERROR);
 CALL(alDeleteSources,1,0x101004);CALL(alDeleteBuffers,1,0x101000);assert(!CALL(alIsSource,s)&&!CALL(alIsBuffer,b));assert(CALL(alcMakeContextCurrent,0));CALL(alcDestroyContext,ctx);assert(CALL(alcCloseDevice,d));
 if(offline){uint32_t extra_ctx=0,extra=tribes_openal_test_loopback(&cpu,&extra_ctx);assert(CALL(alcMakeContextCurrent,extra_ctx));assert(CALL(alcCloseDevice,extra));assert(!CALL(alcGetCurrentContext));if(argc>1&&!strcmp(argv[1],"stale-context"))CALL(alcMakeContextCurrent,extra_ctx);}
 if(argc>1&&!strcmp(argv[1],"stale-device"))CALL(alcGetError,d);
 assert(tribes_free_library(cpu._pointer_offset,TB_OPENAL_MODULE));printf("PASS: cdecl CPU/stack, x87 float, %s source playback (sample %u), owned PCM, queue, context/device lifetime and finite capabilities\n",offline?"native offline mixer":"real CoreAudio",offset);return 0;
}
