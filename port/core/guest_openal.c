/* Native OpenAL 1.1 adapter. The guest owns only 32-bit tokens and copied
 * public data. Native mixer workers never receive a guest pointer. All calls
 * are serialized with device/context destruction. EFX uses copied bounded parameters; other extensions remain
 * unadvertised until their ABI and memory contracts are bridged. */
#include "guest_openal.h"
#include "guest_callback.h"
#include "guest_modules.h"
#include "guest_heap.h"
#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/efx.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {const char *name;uint32_t address;void *function;} OpenALExport;
#include "openal-exports.inc"
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t owner;
static uint32_t serial=0x75000000;
typedef struct Device {uint32_t token;ALCdevice *native;struct Device *next;} Device;
typedef struct Context {uint32_t token;ALCcontext *native;Device *device;struct Context *next;} Context;
static Device *devices;static Context *contexts;
static _Noreturn void stop(const char *why){fprintf(stderr,"TribesPad: OpenAL adapter boundary: %s\n",why);abort();}
static void *guest(_cpu *c,uint32_t a,size_t n){if(!a||n>UINT32_MAX||(uint64_t)a+n>UINT64_C(0x100000000)||c->_pointer_offset>UINTPTR_MAX-a-n)stop("guest span");return (void *)(uintptr_t)(c->_pointer_offset+a);}
static void name_copy(_cpu *c,uint32_t a,char *s){for(unsigned i=0;i<256;i++){memcpy(s+i,guest(c,a+i,1),1);if(!s[i])return;}stop("unterminated name");}
static float f32(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
extern void x87_fld_float(_cpu *,uint32_t),x87_fld_double(_cpu *,uint32_t,uint32_t);
static void enter(_cpu *c){pthread_mutex_lock(&lock);if(owner!=c->_pointer_offset)stop("foreign address space");}
#define BEGIN enter(c)
#define DONE(v) do {uint32_t r_=(v);pthread_mutex_unlock(&lock);return r_;} while(0)
static Device *device(uint32_t t){if(!t)return NULL;for(Device *d=devices;d;d=d->next)if(d->token==t)return d;stop("invalid device token");return NULL;}
static Context *context(uint32_t t){if(!t)return NULL;for(Context *x=contexts;x;x=x->next)if(x->token==t)return x;stop("invalid context token");return NULL;}
#define DN(t) (device(t)?device(t)->native:NULL)
#define CN(t) (context(t)?context(t)->native:NULL)
static uint32_t token(void){if(serial>=0x75ffffff)stop("token exhaustion");return ++serial;}
uint32_t tribes_openal_load(uint64_t base,const char *name){
 if(strcmp(name,"openal32.dll")&&strcmp(name,"soft_oal.dll"))return 0;
 pthread_mutex_lock(&lock);
 if(!owner){TribesNativeExport exports[sizeof openal_exports/sizeof *openal_exports];
  for(unsigned i=0;i<sizeof openal_exports/sizeof *openal_exports;i++)exports[i]=(TribesNativeExport){openal_exports[i].name,0,openal_exports[i].address};
  if(!tribes_module_define_native(base,TB_OPENAL_MODULE,"C:\\Windows\\System32\\openal32.dll",exports,sizeof exports/sizeof *exports))stop("module registration");owner=base;fprintf(stderr,"TribesPad: native OpenAL provider configured (%zu finite exports)\n",sizeof openal_exports/sizeof *openal_exports);
 }
 if(owner!=base)stop("multiple address spaces");pthread_mutex_unlock(&lock);return TB_OPENAL_MODULE;
}
void *tribes_openal_native_lookup(uint32_t a){for(unsigned i=0;i<sizeof openal_exports/sizeof *openal_exports;i++)if(openal_exports[i].address==a)return openal_exports[i].function;return NULL;}
static uint32_t proc(const char *s){for(unsigned i=0;i<sizeof openal_exports/sizeof *openal_exports;i++)if(!strcmp(s,openal_exports[i].name))return openal_exports[i].address;return 0;}
/* Immutable snapshots remain valid for the provider's process lifetime. */
typedef struct String {uint32_t guest;size_t size;char *host;struct String *next;} String;
static String *strings;
static uint32_t string_copy(_cpu *c,const char *s,size_t n){
 if(!s)return 0;for(String *p=strings;p;p=p->next)if(p->size==n&&!memcmp(p->host,s,n))return p->guest;
 uint32_t err=tribes_get_last_error(),a=tribes_global_alloc(c->_pointer_offset,0,n);tribes_set_last_error(err);if(!a)stop("string allocation");
 String *p=calloc(1,sizeof *p);if(!p)stop("string metadata allocation");p->host=malloc(n);if(!p->host)stop("string copy allocation");memcpy(p->host,s,n);memcpy(guest(c,a,n),s,n);p->guest=a;p->size=n;p->next=strings;strings=p;return a;
}
static size_t text_size(const char *s,int list){if(!s)return 0;size_t n=0;do {size_t k=strnlen(s+n,65536-n);if(k==65536-n)stop("native string too long");n+=k+1;if(!list||!s[n])break;}while(n<65535);return n+(list?1:0);}
uint32_t tribes_oa_alcOpenDevice(_cpu *c,uint32_t a){BEGIN;char name[256];if(a)name_copy(c,a,name);ALCdevice *native=alcOpenDevice(a?name:NULL);if(!native)DONE(0);Device *d=calloc(1,sizeof *d);if(!d)stop("device metadata");d->native=native;d->token=token();d->next=devices;devices=d;fprintf(stderr,"TribesPad: native OpenAL device opened, token 0x%x\n",d->token);DONE(d->token);}
uint32_t tribes_oa_alcCloseDevice(_cpu *c,uint32_t a){BEGIN;Device *d=device(a);ALCboolean r=alcCloseDevice(d?d->native:NULL);if(r&&d){Context **q=&contexts;while(*q){if((*q)->device==d){Context *old=*q;*q=old->next;free(old);}else q=&(*q)->next;}Device **p=&devices;while(*p!=d)p=&(*p)->next;*p=d->next;free(d);}DONE(r);}
uint32_t tribes_oa_alcCreateContext(_cpu *c,uint32_t a,uint32_t b){BEGIN;ALCint attrs[129];unsigned n=0;
 /* ALC context attributes are integer key/value pairs, never pointers.
    Copy the bounded list; the pinned native mixer owns value/extension
    validation (including ALC_MAX_AUXILIARY_SENDS used by this client). */
 if(b){for(;;){if(n>=129)stop("context attributes too long");memcpy(&attrs[n],guest(c,b+4*n,4),4);if(!attrs[n++])break;
  if(n>=129)stop("context attributes too long");memcpy(&attrs[n],guest(c,b+4*n,4),4);n++;}}

 Device *d=device(a);ALCcontext *native=alcCreateContext(d?d->native:NULL,b?attrs:NULL);if(!native)DONE(0);Context *x=calloc(1,sizeof *x);if(!x)stop("context metadata");x->native=native;x->device=d;x->token=token();x->next=contexts;contexts=x;DONE(x->token);}
uint32_t tribes_oa_alcMakeContextCurrent(_cpu *c,uint32_t a){BEGIN;DONE(alcMakeContextCurrent(CN(a)));}
uint32_t tribes_oa_alcDestroyContext(_cpu *c,uint32_t a){BEGIN;Context *x=context(a);alcDestroyContext(x?x->native:NULL);if(x){Context **p=&contexts;while(*p!=x)p=&(*p)->next;*p=x->next;free(x);}DONE(0);}
uint32_t tribes_oa_alcGetCurrentContext(_cpu *c){BEGIN;ALCcontext *native=alcGetCurrentContext();if(!native)DONE(0);for(Context *x=contexts;x;x=x->next)if(x->native==native)DONE(x->token);stop("foreign current context");}
uint32_t tribes_oa_alcGetContextsDevice(_cpu *c,uint32_t a){BEGIN;Context *x=context(a);ALCdevice *d=alcGetContextsDevice(x?x->native:NULL);if(!d)DONE(0);if(!x||d!=x->device->native)stop("context device mismatch");DONE(x->device->token);}
uint32_t tribes_oa_alcGetError(_cpu *c,uint32_t a){BEGIN;DONE(alcGetError(DN(a)));}
uint32_t tribes_oa_alGetError(_cpu *c){BEGIN;DONE(alGetError());}
uint32_t tribes_oa_alGetString(_cpu *c,uint32_t a){BEGIN;const char *s=a==AL_EXTENSIONS?"":alGetString(a);DONE(string_copy(c,s,text_size(s,0)));}
uint32_t tribes_oa_alcGetString(_cpu *c,uint32_t a,uint32_t b){BEGIN;const char *s=b==ALC_EXTENSIONS?(alcIsExtensionPresent(DN(a),"ALC_EXT_EFX")?"ALC_EXT_EFX":""):alcGetString(DN(a),b);int list=!a&&b==ALC_DEVICE_SPECIFIER;DONE(string_copy(c,s,text_size(s,list)));}
uint32_t tribes_oa_alIsExtensionPresent(_cpu *c,uint32_t a){BEGIN;char s[256];name_copy(c,a,s);DONE(0);}
uint32_t tribes_oa_alcIsExtensionPresent(_cpu *c,uint32_t a,uint32_t b){BEGIN;char s[256];name_copy(c,b,s);ALCdevice *d=DN(a);DONE(!strcmp(s,"ALC_EXT_EFX")&&alcIsExtensionPresent(d,s));}
uint32_t tribes_oa_alGetEnumValue(_cpu *c,uint32_t a){BEGIN;char s[256];name_copy(c,a,s);DONE(alGetEnumValue(s));}
uint32_t tribes_oa_alGetProcAddress(_cpu *c,uint32_t a){BEGIN;char s[256];name_copy(c,a,s);DONE(alGetProcAddress(s)?proc(s):0);}
uint32_t tribes_oa_alcGetProcAddress(_cpu *c,uint32_t a,uint32_t b){BEGIN;char s[256];name_copy(c,b,s);DONE(alcGetProcAddress(DN(a),s)?proc(s):0);}
uint32_t tribes_oa_alcGetIntegerv(_cpu *c,uint32_t a,uint32_t b,uint32_t n,uint32_t out){BEGIN;if(n>1024)stop("integer query span");ALCint v[1024];if(n)memcpy(v,guest(c,out,4*n),4*n);alcGetIntegerv(DN(a),b,n,v);if(n)memcpy(guest(c,out,4*n),v,4*n);DONE(0);}
#define VOID1(name,type,convert) uint32_t tribes_oa_##name(_cpu *c,uint32_t a){BEGIN;name((type)(convert));DONE(0);}
VOID1(alEnable,ALenum,a) VOID1(alDisable,ALenum,a)
VOID1(alSourcePlay,ALuint,a) VOID1(alSourcePause,ALuint,a) VOID1(alSourceStop,ALuint,a) VOID1(alSourceRewind,ALuint,a)
VOID1(alDistanceModel,ALenum,a) VOID1(alSpeedOfSound,ALfloat,f32(a)) VOID1(alDopplerFactor,ALfloat,f32(a))
#define RET1(name) uint32_t tribes_oa_##name(_cpu *c,uint32_t a){BEGIN;DONE(name(a));}
RET1(alIsEnabled) RET1(alGetBoolean) RET1(alGetInteger) RET1(alIsSource) RET1(alIsBuffer)
uint32_t tribes_oa_alGetFloat(_cpu *c,uint32_t a){BEGIN;float v=alGetFloat(a);uint32_t b;memcpy(&b,&v,4);x87_fld_float(c,b);DONE(0);}
uint32_t tribes_oa_alGetDouble(_cpu *c,uint32_t a){BEGIN;double v=alGetDouble(a);uint64_t b;memcpy(&b,&v,8);x87_fld_double(c,b,b>>32);DONE(0);}
static unsigned width(uint32_t p){switch(p){case AL_POSITION:case AL_VELOCITY:case AL_DIRECTION:return 3;case AL_ORIENTATION:return 6;case AL_AUXILIARY_SEND_FILTER:return 3;
 case AL_METERS_PER_UNIT:case AL_DIRECT_FILTER:case AL_AIR_ABSORPTION_FACTOR:case AL_ROOM_ROLLOFF_FACTOR:case AL_CONE_OUTER_GAINHF:case AL_DIRECT_FILTER_GAINHF_AUTO:case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:return 1;
 case AL_SOURCE_RELATIVE:case AL_CONE_INNER_ANGLE:case AL_CONE_OUTER_ANGLE:case AL_PITCH:case AL_LOOPING:case AL_BUFFER:case AL_GAIN:case AL_MIN_GAIN:case AL_MAX_GAIN:case AL_SOURCE_STATE:case AL_BUFFERS_QUEUED:case AL_BUFFERS_PROCESSED:case AL_REFERENCE_DISTANCE:case AL_ROLLOFF_FACTOR:case AL_CONE_OUTER_GAIN:case AL_MAX_DISTANCE:case AL_SEC_OFFSET:case AL_SAMPLE_OFFSET:case AL_BYTE_OFFSET:case AL_SOURCE_TYPE:case AL_FREQUENCY:case AL_BITS:case AL_CHANNELS:case AL_SIZE:case AL_DOPPLER_FACTOR:case AL_DOPPLER_VELOCITY:case AL_SPEED_OF_SOUND:case AL_DISTANCE_MODEL:return 1;default:stop("unsupported vector parameter");return 0;}}
#define GET_GLOBAL(name,type) uint32_t tribes_oa_##name(_cpu *c,uint32_t p,uint32_t out){BEGIN;if(width(p)!=1)stop("global vector width");type v;memcpy(&v,guest(c,out,sizeof v),sizeof v);name(p,&v);memcpy(guest(c,out,sizeof v),&v,sizeof v);DONE(0);}
GET_GLOBAL(alGetBooleanv,ALboolean) GET_GLOBAL(alGetIntegerv,ALint) GET_GLOBAL(alGetFloatv,ALfloat) GET_GLOBAL(alGetDoublev,ALdouble)
uint32_t tribes_oa_alListenerf(_cpu *c,uint32_t p,uint32_t v){BEGIN;alListenerf(p,f32(v));DONE(0);}
uint32_t tribes_oa_alListener3f(_cpu *c,uint32_t p,uint32_t x,uint32_t y,uint32_t z){BEGIN;alListener3f(p,f32(x),f32(y),f32(z));DONE(0);}
#define LISTENER_VECTOR(name,type,get) uint32_t tribes_oa_##name(_cpu *c,uint32_t p,uint32_t a){BEGIN;type v[6];size_t n=sizeof(type)*width(p);memcpy(v,guest(c,a,n),n);name(p,v);if(get)memcpy(guest(c,a,n),v,n);DONE(0);}
LISTENER_VECTOR(alListenerfv,ALfloat,0) LISTENER_VECTOR(alGetListeneriv,ALint,1) LISTENER_VECTOR(alGetListenerfv,ALfloat,1)
#define OBJECT_SCALAR(name,type,convert) uint32_t tribes_oa_##name(_cpu *c,uint32_t o,uint32_t p,uint32_t v){BEGIN;name(o,p,(type)(convert));DONE(0);}
OBJECT_SCALAR(alSourcei,ALint,v) OBJECT_SCALAR(alSourcef,ALfloat,f32(v)) OBJECT_SCALAR(alBufferi,ALint,v)
uint32_t tribes_oa_alSource3f(_cpu *c,uint32_t o,uint32_t p,uint32_t x,uint32_t y,uint32_t z){BEGIN;alSource3f(o,p,f32(x),f32(y),f32(z));DONE(0);}
uint32_t tribes_oa_alSource3i(_cpu *c,uint32_t o,uint32_t p,uint32_t x,uint32_t y,uint32_t z){BEGIN;alSource3i(o,p,x,y,z);DONE(0);}
#define OBJECT_VECTOR(name,type,get,scalar) uint32_t tribes_oa_##name(_cpu *c,uint32_t o,uint32_t p,uint32_t a){BEGIN;unsigned w=width(p);if((scalar)&&w!=1)stop("scalar output width");type v[6];size_t n=sizeof(type)*w;memcpy(v,guest(c,a,n),n);name(o,p,v);if(get)memcpy(guest(c,a,n),v,n);DONE(0);}
OBJECT_VECTOR(alSourceiv,ALint,0,0) OBJECT_VECTOR(alSourcefv,ALfloat,0,0) OBJECT_VECTOR(alGetSourceiv,ALint,1,0) OBJECT_VECTOR(alGetSourcefv,ALfloat,1,0) OBJECT_VECTOR(alGetBufferi,ALint,1,1) OBJECT_VECTOR(alGetBufferf,ALfloat,1,1)
uint32_t tribes_oa_alGetSource3f(_cpu *c,uint32_t o,uint32_t p,uint32_t a,uint32_t b,uint32_t d){BEGIN;float x,y,z;memcpy(&x,guest(c,a,4),4);memcpy(&y,guest(c,b,4),4);memcpy(&z,guest(c,d,4),4);alGetSource3f(o,p,&x,&y,&z);memcpy(guest(c,a,4),&x,4);memcpy(guest(c,b,4),&y,4);memcpy(guest(c,d,4),&z,4);DONE(0);}
#define IDS(name,output) uint32_t tribes_oa_##name(_cpu *c,uint32_t n,uint32_t a){BEGIN;if(n>65536)stop("object count");ALuint *v=n?malloc(n*4):NULL;if(n&&!v)stop("object array allocation");if(n)memcpy(v,guest(c,a,n*4),n*4);name(n,v);if((output)&&n)memcpy(guest(c,a,n*4),v,n*4);free(v);DONE(0);}
IDS(alGenSources,1) IDS(alDeleteSources,0) IDS(alGenBuffers,1) IDS(alDeleteBuffers,0) IDS(alSourcePlayv,0) IDS(alSourceStopv,0)
#define QUEUE(name,output) uint32_t tribes_oa_##name(_cpu *c,uint32_t o,uint32_t n,uint32_t a){BEGIN;if(n>65536)stop("queue count");ALuint *v=n?malloc(n*4):NULL;if(n&&!v)stop("queue allocation");if(n)memcpy(v,guest(c,a,n*4),n*4);name(o,n,v);if((output)&&n)memcpy(guest(c,a,n*4),v,n*4);free(v);DONE(0);}
QUEUE(alSourceQueueBuffers,0) QUEUE(alSourceUnqueueBuffers,1)
uint32_t tribes_oa_alBufferData(_cpu *c,uint32_t o,uint32_t format,uint32_t a,uint32_t n,uint32_t hz){BEGIN;
 if(format!=AL_FORMAT_MONO8&&format!=AL_FORMAT_MONO16&&format!=AL_FORMAT_STEREO8&&format!=AL_FORMAT_STEREO16)stop("unbridged sample format");
 if(n>64*1024*1024)stop("PCM size");void *data=n?malloc(n):NULL;if(n&&!data)stop("PCM allocation");if(n&&a)memcpy(data,guest(c,a,n),n);alBufferData(o,format,a?data:NULL,n,hz);free(data);DONE(0);}

/* Native EFX object IDs are integers. All pointer arguments below refer only
 * to bounded host copies; the mixer never retains guest memory. */
IDS(alGenEffects,1) IDS(alDeleteEffects,0)
IDS(alGenFilters,1) IDS(alDeleteFilters,0)
IDS(alGenAuxiliaryEffectSlots,1) IDS(alDeleteAuxiliaryEffectSlots,0)
RET1(alIsEffect) RET1(alIsFilter) RET1(alIsAuxiliaryEffectSlot)
OBJECT_SCALAR(alEffecti,ALint,v) OBJECT_SCALAR(alEffectf,ALfloat,f32(v))
OBJECT_SCALAR(alFilteri,ALint,v) OBJECT_SCALAR(alFilterf,ALfloat,f32(v))
OBJECT_SCALAR(alAuxiliaryEffectSloti,ALint,v) OBJECT_SCALAR(alAuxiliaryEffectSlotf,ALfloat,f32(v))
/* Only EAX reverb's two floating pan parameters consume three components.
 * The same numeric enums in other effect types are scalar. Query under the
 * adapter lock, which also serializes effect type changes and destruction. */
static unsigned effect_float_width(ALuint effect,ALenum param){
 ALint type=AL_EFFECT_NULL;
 if(param==AL_EAXREVERB_REFLECTIONS_PAN||param==AL_EAXREVERB_LATE_REVERB_PAN){
  alGetEffecti(effect,AL_EFFECT_TYPE,&type);
  if(type==AL_EFFECT_EAXREVERB)return 3;
 }
 return 1;
}
#define EFX_VECTOR(name,type,get,count) uint32_t tribes_oa_##name(_cpu *c,uint32_t o,uint32_t p,uint32_t a){BEGIN;unsigned n=(count);type v[3];size_t bytes=sizeof(type)*n;memcpy(v,guest(c,a,bytes),bytes);name(o,p,v);if(get)memcpy(guest(c,a,bytes),v,bytes);DONE(0);}
EFX_VECTOR(alEffectiv,ALint,0,1)
EFX_VECTOR(alEffectfv,ALfloat,0,effect_float_width(o,p))
EFX_VECTOR(alGetEffecti,ALint,1,1) EFX_VECTOR(alGetEffectiv,ALint,1,1)
EFX_VECTOR(alGetEffectf,ALfloat,1,1)
EFX_VECTOR(alGetEffectfv,ALfloat,1,effect_float_width(o,p))
EFX_VECTOR(alFilteriv,ALint,0,1) EFX_VECTOR(alFilterfv,ALfloat,0,1)
EFX_VECTOR(alGetFilteri,ALint,1,1) EFX_VECTOR(alGetFilteriv,ALint,1,1)
EFX_VECTOR(alGetFilterf,ALfloat,1,1) EFX_VECTOR(alGetFilterfv,ALfloat,1,1)
EFX_VECTOR(alAuxiliaryEffectSlotiv,ALint,0,1) EFX_VECTOR(alAuxiliaryEffectSlotfv,ALfloat,0,1)
EFX_VECTOR(alGetAuxiliaryEffectSloti,ALint,1,1) EFX_VECTOR(alGetAuxiliaryEffectSlotiv,ALint,1,1)
EFX_VECTOR(alGetAuxiliaryEffectSlotf,ALfloat,1,1) EFX_VECTOR(alGetAuxiliaryEffectSlotfv,ALfloat,1,1)

#ifdef TB_OPENAL_TEST
/* Host-only offline mixer fixture; never present in the game build or exports. */
#include <AL/alext.h>
uint32_t tribes_openal_test_loopback(_cpu *c,uint32_t *ctx){
 BEGIN;ALCdevice *native=alcLoopbackOpenDeviceSOFT(NULL);if(!native)stop("test loopback device");
 ALCint attrs[]={ALC_FORMAT_CHANNELS_SOFT,ALC_STEREO_SOFT,ALC_FORMAT_TYPE_SOFT,ALC_FLOAT_SOFT,ALC_FREQUENCY,48000,0};
 ALCcontext *nc=alcCreateContext(native,attrs);if(!nc)stop("test loopback context");
 Device *d=calloc(1,sizeof *d);Context *x=calloc(1,sizeof *x);if(!d||!x)stop("test metadata");
 d->native=native;d->token=token();d->next=devices;devices=d;x->native=nc;x->device=d;x->token=token();x->next=contexts;contexts=x;*ctx=x->token;DONE(d->token);
}
void tribes_openal_test_render(_cpu *c,uint32_t d,float *output,unsigned frames){enter(c);alcRenderSamplesSOFT(DN(d),output,frames);pthread_mutex_unlock(&lock);}
#endif
