/* Native DirectSound device lifetime. The output is a real SDL/CoreAudio
   logical device; no Windows DLL, generated code or successful buffer stub.
   ABI constants: ref/contracts/directsound/dsound-wine-10.h. */
#include "guest_dsound.h"
#include "guest_heap.h"
#include "guest_modules.h"
extern void *tribes_lookup(uint32_t);
#include <SDL3/SDL.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define INVALID 0x80070057u
#define NODRIVER 0x88780078u
#define NOINTERFACE 0x80004002u
#define OOM 0x8007000eu
#define UNSUPPORTED 0x80004001u
#define GENERIC 0x80004005u
typedef struct Device {uint64_t base;uint32_t object,refs,level,window;SDL_AudioDeviceID native;SDL_AudioSpec format;int frames;uint32_t primary,listener_state[16],listener_active[16];struct Device *next;} Device;
static Device *devices;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
/* Namespace for process-local SDL physical device identities. */
static const unsigned char device_tail[12]={0x54,0x50,0x41,0x44,0x80,0x32,0x44,0x53,0x4f,0x55,0x4e,0x44};
static const unsigned char ds_iid[16]={0x83,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0xb,0xe5,0x60};
static const unsigned char unknown_iid[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
extern int tribes_sdl_has_window_handle(uint64_t,uint32_t);
typedef struct {const char *name;uint32_t address;void *function;} SoundExport;
#include "dsound-exports.inc"
int tribes_dsound_configure(uint64_t base){
 const TribesNativeExport e[]={{"DirectSoundCreate",0,TB_DS_CREATE},{"DirectSoundEnumerateA",0,TB_DS_ENUMERATE}};
 return tribes_module_define_native(base,TB_DSOUND_MODULE,"C:\\Windows\\System32\\dsound.dll",e,2);
}
void *tribes_dsound_native_lookup(uint32_t address){for(unsigned i=0;i<sizeof sound_exports/sizeof *sound_exports;i++)if(sound_exports[i].address==address)return sound_exports[i].function;return NULL;}
static void *ptr(uint64_t base,uint32_t a,uint32_t n){if(!a||!n||(uint64_t)a+n>UINT64_C(0x100000000)||base>UINTPTR_MAX-a-(n-1))return NULL;return (void *)(uintptr_t)(base+a);}
static Device *find(uint64_t base,uint32_t object){for(Device *d=devices;d;d=d->next)if(d->base==base&&d->object==object)return d;fprintf(stderr,"TribesPad: invalid DirectSound object %x\n",object);abort();}
static uint32_t alloc(uint64_t base,uint32_t bytes){uint32_t err=tribes_get_last_error(),a=tribes_global_alloc(base,0x40,bytes);tribes_set_last_error(err);return a;}
static void discard(uint64_t base,uint32_t object){uint32_t err=tribes_get_last_error();if(tribes_global_free(base,object))abort();tribes_set_last_error(err);}
static SDL_AudioDeviceID selection(uint64_t base,uint32_t guid){
 unsigned char g[16]={0},zero[16]={0};if(guid)memcpy(g,ptr(base,guid,16),16);
 if(!memcmp(g,zero,16))return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
 const unsigned char def[16]={0,0,0xf0,0xde,0x6d,0x9c,0xed,0x47,0xaa,0xf1,0x4d,0xda,0x8f,0x2b,0x5c,3};
 if(!memcmp(g,def,16))return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
 if(memcmp(g+4,device_tail,12))return 0;
 uint32_t id;memcpy(&id,g,4);int count=0;SDL_AudioDeviceID *ids=SDL_GetAudioPlaybackDevices(&count);int found=0;
 for(int i=0;i<count;i++)if(ids[i]==id)found=1;SDL_free(ids);return found?id:0;
}
uint32_t tribes_dsound_create(uint64_t base,uint32_t guid,uint32_t output,uint32_t outer){
 uint32_t *out=ptr(base,output,4);if(!out)return INVALID;*out=0;if(outer)return 0x80040110u;
 if(guid&&!ptr(base,guid,16))return INVALID;
 if(!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO))return NODRIVER;
 SDL_AudioDeviceID id=selection(base,guid);if(!id)return NODRIVER;
 Device *d=calloc(1,sizeof *d);if(!d)return OOM;
 d->native=SDL_OpenAudioDevice(id,NULL);if(!d->native){free(d);return NODRIVER;}
 if(!SDL_GetAudioDeviceFormat(d->native,&d->format,&d->frames)||!SDL_PauseAudioDevice(d->native)){SDL_CloseAudioDevice(d->native);free(d);return GENERIC;}
 d->object=alloc(base,48);if(!d->object){SDL_CloseAudioDevice(d->native);free(d);return OOM;}
 uint32_t table[12];table[0]=d->object+4;for(unsigned i=0;i<11;i++)table[i+1]=TB_DS_METHOD(i);memcpy(ptr(base,d->object,sizeof table),table,sizeof table);
 d->listener_state[0]=64;d->listener_state[9]=d->listener_state[11]=d->listener_state[13]=d->listener_state[14]=d->listener_state[15]=0x3f800000u;memcpy(d->listener_active,d->listener_state,64);
 d->base=base;d->refs=1;d->level=1;pthread_mutex_lock(&lock);d->next=devices;devices=d;pthread_mutex_unlock(&lock);*out=d->object;
 fprintf(stderr,"TribesPad: DirectSound device opened object=0x%08x rate=%d channels=%d frames=%d\n",d->object,d->format.freq,d->format.channels,d->frames);return 0;
}
uint32_t tribes_ds_query(uint64_t base,uint32_t object,uint32_t iid,uint32_t output){
 uint32_t *out=ptr(base,output,4);void *id=ptr(base,iid,16);if(!out||!id)return INVALID;unsigned char g[16];memcpy(g,id,16);*out=0;
 pthread_mutex_lock(&lock);Device *d=find(base,object);int supported=!memcmp(g,unknown_iid,16)||!memcmp(g,ds_iid,16);if(supported){if(d->refs==UINT32_MAX)abort();d->refs++;*out=object;}pthread_mutex_unlock(&lock);return supported?0:NOINTERFACE;
}
uint32_t tribes_ds_addref(uint64_t base,uint32_t object){pthread_mutex_lock(&lock);Device *d=find(base,object);if(d->refs==UINT32_MAX)abort();uint32_t r=++d->refs;pthread_mutex_unlock(&lock);return r;}
uint32_t tribes_ds_release(uint64_t base,uint32_t object){
 pthread_mutex_lock(&lock);Device *d=find(base,object);uint32_t r=--d->refs;if(!r){Device **p=&devices;while(*p!=d)p=&(*p)->next;*p=d->next;}pthread_mutex_unlock(&lock);
 if(!r){SDL_CloseAudioDevice(d->native);discard(base,object);free(d);}return r;
}
uint32_t tribes_ds_caps(uint64_t base,uint32_t object,uint32_t output){
 uint32_t *out=ptr(base,output,96);if(!out)return INVALID;uint32_t size;memcpy(&size,out,4);if(size<96)return INVALID;
 pthread_mutex_lock(&lock);(void)find(base,object);
 /* PCM conversion/mixing is software-owned; no hardware/3D memory claimed.
    EMULDRIVER means WaveOut emulation, not software mixing. This device
    feeds SDL/CoreAudio directly and does not use waveform-audio functions. */
 uint32_t caps[24]={96,0xf1f,100,200000,1};
 memcpy(out,caps,sizeof caps);pthread_mutex_unlock(&lock);return 0;
}
uint32_t tribes_ds_cooperative(uint64_t base,uint32_t object,uint32_t window,uint32_t level){
 pthread_mutex_lock(&lock);(void)find(base,object);pthread_mutex_unlock(&lock);
 if(level<1||level>4||!tribes_sdl_has_window_handle(base,window))return INVALID;
 if(level==4)return UNSUPPORTED; /* Direct writes to the primary mix are unsupported. */
 if(level==3){fprintf(stderr,"TribesPad: unsupported DirectSound exclusive focus\n");abort();}
 pthread_mutex_lock(&lock);Device *d=find(base,object);d->window=window;d->level=level;pthread_mutex_unlock(&lock);return 0;
}
uint32_t tribes_ds_compact(uint64_t base,uint32_t object){pthread_mutex_lock(&lock);Device *d=find(base,object);uint32_t r=d->level<2?0x88780046u:0;pthread_mutex_unlock(&lock);return r;}
uint32_t tribes_ds_initialize(uint64_t base,uint32_t object,uint32_t guid){(void)guid;pthread_mutex_lock(&lock);(void)find(base,object);pthread_mutex_unlock(&lock);return 0x88780082u;}
/* All COM objects are indexed under lock. Audio callbacks take only SDL's
   stream lock and read host-owned PCM, never guest memory or this registry. */
typedef struct Buffer {
 Device *owner;uint32_t object,listener,refs,flags,bytes,data,frequency;
 SDL_AudioStream *stream;SDL_AudioSpec pcm;unsigned char *samples;
 uint64_t submitted;uint32_t start;int playing,looping,bound;
 int32_t volume,pan;float left,right;
 uint32_t locked,p1,n1,p2,n2;
 struct Buffer *next;
} Buffer;
static Buffer *buffers;
static Buffer *buffer_find(uint64_t base,uint32_t object){
 for(Buffer *b=buffers;b;b=b->next)if(b->owner->base==base&&(b->object==object||(b->listener&&b->listener==object)))return b;
 fprintf(stderr,"TribesPad: invalid DirectSound buffer %x\n",object);abort();
}
static unsigned block_size(Buffer *b){return b->pcm.channels*SDL_AUDIO_BITSIZE(b->pcm.format)/8;}
static void stream_lock(Buffer *b){if(!SDL_LockAudioStream(b->stream))abort();}
static void stream_unlock(Buffer *b){if(!SDL_UnlockAudioStream(b->stream))abort();}
/* Called with the stream lock. SDL removes queued source frames only when
   converting them for an actual consumer; there is no wall-clock cursor. */
static uint32_t position(Buffer *b){
 int queued=SDL_GetAudioStreamQueued(b->stream);if(queued<0||queued%8||(uint64_t)queued/8>b->submitted)abort();
 uint64_t frames=b->submitted-(uint32_t)queued/8;uint64_t offset=b->start+frames*block_size(b);
 if(!b->looping&&offset>=b->bytes){b->playing=0;return 0;}
 return (uint32_t)(offset%b->bytes);
}
static uint32_t write_position(Buffer *b){
 /* The submitted tail is already held by SDL and unavailable for rewriting.
    Reserve one native device quantum ahead for its next asynchronous pull. */
 uint64_t lead=((uint64_t)b->owner->frames*b->frequency+b->owner->format.freq-1)/b->owner->format.freq;
 if(lead>=b->bytes/block_size(b))lead=b->bytes/block_size(b)-1;
 return (uint32_t)((b->start+(b->submitted+lead)*block_size(b))%b->bytes);
}
static void SDLCALL supply_pcm(void *context,SDL_AudioStream *stream,int additional,int total){
 (void)total;Buffer *b=context;if(!b->playing||additional<=0)return;
 uint32_t frames=((uint32_t)additional+7)/8,align=block_size(b),count=b->bytes/align;
 float converted[2048];
 while(frames){
  uint64_t absolute=b->start/align+b->submitted;
  if(!b->looping&&absolute>=count)break;
  uint32_t at=(uint32_t)(absolute%count),n=count-at;if(n>frames)n=frames;if(n>1024)n=1024;
  for(uint32_t i=0;i<n;i++){
   float c[2];for(int channel=0;channel<b->pcm.channels;channel++){
    uint32_t p=(at+i)*align+channel*(SDL_AUDIO_BITSIZE(b->pcm.format)/8);
    if(b->pcm.format==SDL_AUDIO_U8)c[channel]=((int)b->samples[p]-128)/128.0f;
    else {int16_t sample;memcpy(&sample,b->samples+p,2);c[channel]=sample/32768.0f;}
   }
   if(b->pcm.channels==1)c[1]=c[0];converted[2*i]=c[0]*b->left;converted[2*i+1]=c[1]*b->right;
  }
  if(!SDL_PutAudioStreamData(stream,converted,(int)n*8)){fputs("TribesPad: DirectSound native sample delivery failed\n",stderr);abort();}
  b->submitted+=n;frames-=n;
  if(!b->looping&&b->start/align+b->submitted>=count){if(!SDL_FlushAudioStream(stream))abort();break;}
 }
}
static uint32_t read_pcm(uint64_t base,uint32_t address,SDL_AudioSpec *pcm){
 uint16_t w[8];if(!ptr(base,address,16))return INVALID;memcpy(w,ptr(base,address,16),16);uint32_t rate,bytes;memcpy(&rate,w+2,4);memcpy(&bytes,w+4,4);
 if(w[0]!=1){fprintf(stderr,"TribesPad: unsupported DirectSound format tag=%u\n",w[0]);abort();}
 if((w[1]!=1&&w[1]!=2)||(w[7]!=8&&w[7]!=16)||rate<100||rate>200000||w[6]!=w[1]*(w[7]/8)||bytes!=rate*w[6])return INVALID;
 *pcm=(SDL_AudioSpec){w[7]==8?SDL_AUDIO_U8:SDL_AUDIO_S16LE,w[1],(int)rate};return 0;
}
uint32_t tribes_ds_create_buffer(uint64_t base,uint32_t object,uint32_t desc,uint32_t output,uint32_t outer){
 uint32_t *out=ptr(base,output,4);if(!out)return INVALID;*out=0;if(outer)return 0x80040110u;
 uint32_t size,*input=ptr(base,desc,4);if(!input)return INVALID;memcpy(&size,input,4);if(size!=20&&size!=36)return INVALID;
 uint32_t words[9]={0};if(!ptr(base,desc,size))return INVALID;memcpy(words,ptr(base,desc,size),size);
 fprintf(stderr,"TribesPad: DirectSound buffer request size=%u flags=0x%x bytes=%u format=0x%x\n",words[0],words[1],words[2],words[4]);
 int is_primary=(words[1]&1)!=0;
 if(is_primary&&(words[1]&~0x11u))return INVALID;
 /* The reached software/global-focus profile. 3D, hardware allocation and
    focus-dependent secondary playback need their own real implementations. */
 if(!is_primary&&((words[1]&~0x180eau)||!(words[1]&0x8000))){fprintf(stderr,"TribesPad: unsupported DirectSound buffer flags=0x%x bytes=%u format=0x%x\n",words[1],words[2],words[4]);abort();}
 if(words[3]||(is_primary&&(words[2]||words[4])))return INVALID;
 SDL_AudioSpec pcm;uint32_t result=0;if(!is_primary){result=read_pcm(base,words[4],&pcm);if(result)return result;if(words[2]<4||words[2]>0xfffffff||words[2]%(pcm.channels*SDL_AUDIO_BITSIZE(pcm.format)/8))return INVALID;}
 pthread_mutex_lock(&lock);Device *d=find(base,object);
 if(is_primary&&d->primary){Buffer *b=buffer_find(base,d->primary);if((words[1]&0x10)&&!b->listener){pthread_mutex_unlock(&lock);return 0x8878001eu;}if(b->refs==UINT32_MAX)abort();b->refs++;*out=b->object;pthread_mutex_unlock(&lock);return 0;}
 if(is_primary)pcm=(SDL_AudioSpec){SDL_AUDIO_S16LE,2,d->format.freq};
 Buffer *b=calloc(1,sizeof *b);if(!b){pthread_mutex_unlock(&lock);return OOM;}
 b->owner=d;b->refs=1;b->flags=words[1];b->bytes=words[2];b->pcm=pcm;b->frequency=pcm.freq;b->left=b->right=1;
 SDL_AudioSpec source=is_primary?pcm:(SDL_AudioSpec){SDL_AUDIO_F32,2,pcm.freq};
 b->stream=SDL_CreateAudioStream(&source,&d->format);if(!b->stream){result=OOM;goto fail;}
 if(is_primary){if(!SDL_BindAudioStream(d->native,b->stream)){result=GENERIC;goto fail;}}
 else {
  b->samples=malloc(b->bytes);b->data=alloc(base,b->bytes);if(!b->samples||!b->data){result=OOM;goto fail;}
  int silence=pcm.format==SDL_AUDIO_U8?128:0;memset(b->samples,silence,b->bytes);memset(ptr(base,b->data,b->bytes),silence,b->bytes);
  if(!SDL_SetAudioStreamGetCallback(b->stream,supply_pcm,b)){result=GENERIC;goto fail;}
 }
 if(is_primary&&(words[1]&0x10)){
  b->listener=alloc(base,76);if(!b->listener){result=OOM;goto fail;}
  uint32_t v[19];v[0]=b->listener+4;for(unsigned i=0;i<18;i++)v[i+1]=TB_DSL_METHOD(i);memcpy(ptr(base,b->listener,sizeof v),v,sizeof v);
 }
 b->object=alloc(base,88);if(!b->object){result=OOM;goto fail;}
 uint32_t table[22];table[0]=b->object+4;for(unsigned i=0;i<21;i++)table[i+1]=TB_DSB_METHOD(i);memcpy(ptr(base,b->object,sizeof table),table,sizeof table);
 if(d->refs==UINT32_MAX)abort();d->refs++;if(is_primary)d->primary=b->object;b->next=buffers;buffers=b;*out=b->object;
 fprintf(stderr,"TribesPad: DirectSound %s buffer object=0x%08x rate=%d channels=%d bits=%u bytes=%u\n",is_primary?"primary":"secondary",b->object,pcm.freq,pcm.channels,SDL_AUDIO_BITSIZE(pcm.format),b->bytes);
 pthread_mutex_unlock(&lock);return 0;
fail:
 if(b->stream)SDL_DestroyAudioStream(b->stream);if(b->listener)discard(base,b->listener);if(b->data)discard(base,b->data);free(b->samples);free(b);pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_dsb_query(uint64_t base,uint32_t object,uint32_t iid,uint32_t output){
 const unsigned char buffer_iid[16]={0x85,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0xb,0xe5,0x60};
 uint32_t *out=ptr(base,output,4);void *id=ptr(base,iid,16);if(!out||!id)return INVALID;unsigned char g[16];memcpy(g,id,16);*out=0;
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);int supported=!memcmp(g,unknown_iid,16)||!memcmp(g,buffer_iid,16);
 uint32_t selected=b->object;
 unsigned char listener_iid[16];memcpy(listener_iid,buffer_iid,16);listener_iid[0]=0x84;
 if(!memcmp(g,listener_iid,16)&&b->listener){supported=1;selected=b->listener;}
 if(supported){if(b->refs==UINT32_MAX)abort();b->refs++;*out=selected;}pthread_mutex_unlock(&lock);return supported?0:NOINTERFACE;
}
uint32_t tribes_dsb_addref(uint64_t base,uint32_t object){pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);if(b->refs==UINT32_MAX)abort();uint32_t r=++b->refs;pthread_mutex_unlock(&lock);return r;}
uint32_t tribes_dsb_release(uint64_t base,uint32_t object){
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);uint32_t owner=b->owner->object,r=--b->refs;
 if(!r){Buffer **p=&buffers;while(*p!=b)p=&(*p)->next;*p=b->next;if(b->flags&1)b->owner->primary=0;
  SDL_DestroyAudioStream(b->stream);if(b->data)discard(base,b->data);if(b->listener)discard(base,b->listener);discard(base,b->object);free(b->samples);free(b);}
 pthread_mutex_unlock(&lock);if(!r)tribes_ds_release(base,owner);return r;
}
uint32_t tribes_dsb_format(uint64_t base,uint32_t object,uint32_t output,uint32_t capacity,uint32_t written){
 if(!output&&!written)return INVALID;if(output&&capacity<18)return INVALID;
 void *out=output?ptr(base,output,18):NULL;uint32_t *count=written?ptr(base,written,4):NULL;if((output&&!out)||(written&&!count))return INVALID;
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);SDL_AudioSpec pcm=b->pcm;int ok=!(b->flags&1)||SDL_GetAudioStreamFormat(b->stream,&pcm,NULL);pthread_mutex_unlock(&lock);if(!ok)return GENERIC;
 uint16_t wave[9]={1,(uint16_t)pcm.channels,0,0,0,0,0,(uint16_t)SDL_AUDIO_BITSIZE(pcm.format),0};
 uint32_t rate=pcm.freq,bytes=pcm.channels*(wave[7]/8)*rate;wave[6]=pcm.channels*(wave[7]/8);memcpy(wave+2,&rate,4);memcpy(wave+4,&bytes,4);
 if(out)memcpy(out,wave,18);if(count)*count=18;return 0;
}
uint32_t tribes_dsb_set_format(uint64_t base,uint32_t object,uint32_t format){
 SDL_AudioSpec pcm;uint32_t result=read_pcm(base,format,&pcm);if(result)return result;
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);
 result=!(b->flags&1)?INVALID:b->owner->level<2?0x88780046u:SDL_SetAudioStreamFormat(b->stream,&pcm,NULL)?0:GENERIC;
 if(!result){b->pcm=pcm;fprintf(stderr,"TribesPad: DirectSound primary format rate=%d channels=%d bits=%u\n",pcm.freq,pcm.channels,SDL_AUDIO_BITSIZE(pcm.format));}pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_dsb_play(uint64_t base,uint32_t object,uint32_t reserved,uint32_t priority,uint32_t flags){
 if(reserved||priority||(flags&~1u))return INVALID;pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);int ok;
 if(b->flags&1)ok=flags==1?SDL_ResumeAudioDevice(b->owner->native):0;
 else {stream_lock(b);if(!b->playing||b->looping!=(int)(flags&1)){b->start=position(b);b->submitted=0;if(!SDL_ClearAudioStream(b->stream))abort();}b->looping=flags&1;b->playing=1;stream_unlock(b);ok=(b->bound||SDL_BindAudioStream(b->owner->native,b->stream));if(ok){b->bound=1;ok=SDL_ResumeAudioDevice(b->owner->native);}
  if(!ok){stream_lock(b);b->playing=0;stream_unlock(b);}else fprintf(stderr,"TribesPad: DirectSound PCM playback object=0x%08x looping=%u\n",object,flags&1);}
 uint32_t result=ok?0:(b->flags&1&&flags!=1?INVALID:GENERIC);pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_dsb_stop(uint64_t base,uint32_t object){
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);int ok=1;
 if(b->flags&1)ok=SDL_PauseAudioDevice(b->owner->native);
 else {SDL_UnbindAudioStream(b->stream);b->bound=0;stream_lock(b);b->start=position(b);b->submitted=0;b->playing=0;ok=SDL_ClearAudioStream(b->stream);stream_unlock(b);}
 pthread_mutex_unlock(&lock);return ok?0:GENERIC;
}
uint32_t tribes_dsb_status(uint64_t base,uint32_t object,uint32_t output){
 uint32_t *out=ptr(base,output,4);if(!out)return INVALID;pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);
 if(b->flags&1)*out=SDL_AudioDevicePaused(b->owner->native)?0:5;
 else {stream_lock(b);(void)position(b);*out=b->playing?(1u|(b->looping?4u:0)):0;stream_unlock(b);}pthread_mutex_unlock(&lock);return 0;
}
uint32_t tribes_dsb_caps(uint64_t base,uint32_t object,uint32_t output){
 uint32_t *out=ptr(base,output,20);if(!out)return INVALID;uint32_t size;memcpy(&size,out,4);if(size<20)return INVALID;
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);uint32_t caps[5]={20,b->flags|8,b->bytes,0,0};
 if(b->flags&1){pthread_mutex_unlock(&lock);return UNSUPPORTED;}memcpy(out,caps,20);pthread_mutex_unlock(&lock);return 0;
}
uint32_t tribes_dsb_position(uint64_t base,uint32_t object,uint32_t play,uint32_t write){
 uint32_t *p=play?ptr(base,play,4):NULL,*w=write?ptr(base,write,4):NULL;if((play&&!p)||(write&&!w))return INVALID;
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);if(b->flags&1){pthread_mutex_unlock(&lock);return UNSUPPORTED;}
 stream_lock(b);uint32_t at=position(b);if(p)*p=at;if(w)*w=b->playing?write_position(b):at;stream_unlock(b);pthread_mutex_unlock(&lock);return 0;
}
uint32_t tribes_dsb_lock(uint64_t base,uint32_t object,uint32_t offset,uint32_t bytes,uint32_t p1,uint32_t n1,uint32_t p2,uint32_t n2,uint32_t flags){
 uint32_t *a=ptr(base,p1,4),*n=ptr(base,n1,4),*c=p2?ptr(base,p2,4):NULL,*m=n2?ptr(base,n2,4):NULL;
 if(!a||!n||(p2&&!c)||(n2&&!m)||!!p2!=!!n2||(flags&~3u))return INVALID;
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);uint32_t result=0;
 if(b->flags&1){pthread_mutex_unlock(&lock);return UNSUPPORTED;}stream_lock(b);
 if(flags&1)offset=b->playing?write_position(b):position(b);if(flags&2){offset=0;bytes=b->bytes;}
 if(b->locked||offset>=b->bytes||!bytes||bytes>b->bytes){result=INVALID;goto done;}
 uint32_t first=b->bytes-offset;if(first>bytes)first=bytes;uint32_t second=bytes-first;
 if(second&&!c){result=INVALID;goto done;}
 b->p1=b->data+offset;b->n1=first;b->p2=second?b->data:0;b->n2=second;b->locked=1;
 *a=b->p1;*n=first;if(c)*c=b->p2;if(m)*m=second;
done:stream_unlock(b);pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_dsb_unlock(uint64_t base,uint32_t object,uint32_t p1,uint32_t n1,uint32_t p2,uint32_t n2){
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);if(b->flags&1){pthread_mutex_unlock(&lock);return UNSUPPORTED;}stream_lock(b);uint32_t result=0;
 if(!b->locked||p1!=b->p1||p2!=b->p2||n1>b->n1||n2>b->n2){result=INVALID;goto done;}
 if(n1)memcpy(b->samples+(p1-b->data),ptr(base,p1,n1),n1);if(n2)memcpy(b->samples,ptr(base,p2,n2),n2);b->locked=0;
done:stream_unlock(b);pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_dsb_set_position(uint64_t base,uint32_t object,uint32_t at){
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);if((b->flags&1)||at>=b->bytes){pthread_mutex_unlock(&lock);return INVALID;}
 stream_lock(b);b->start=at-at%block_size(b);b->submitted=0;int ok=SDL_ClearAudioStream(b->stream);stream_unlock(b);pthread_mutex_unlock(&lock);return ok?0:GENERIC;
}
static uint32_t control(uint64_t base,uint32_t object,uint32_t value,int kind,int set){
 uint32_t *out=set?NULL:ptr(base,value,4);if(!set&&!out)return INVALID;
 pthread_mutex_lock(&lock);Buffer *b=buffer_find(base,object);uint32_t required=kind==0?0x80:kind==1?0x40:0x20;
 if(!(b->flags&required)){pthread_mutex_unlock(&lock);return 0x8878001eu;}
 stream_lock(b);uint32_t result=0;
 if(!set)*out=kind==0?(uint32_t)b->volume:kind==1?(uint32_t)b->pan:b->frequency;
 else if(kind==2){
  if(!value)value=b->pcm.freq;
  if(value<100||value>200000)result=INVALID;
  else {SDL_AudioSpec source={SDL_AUDIO_F32,2,(int)value};if(!SDL_SetAudioStreamFormat(b->stream,&source,NULL))result=GENERIC;else b->frequency=value;}
 }else {
  int32_t v=(int32_t)value;if((kind==0&&(v>0||v< -10000))||(kind==1&&(v< -10000||v>10000)))result=INVALID;
  else {if(kind==0)b->volume=v;else b->pan=v;
   int32_t left=b->volume-(b->pan>0?b->pan:0),right=b->volume+(b->pan<0?b->pan:0);
   b->left=left<=-10000?0:powf(10.0f,left/2000.0f);b->right=right<=-10000?0:powf(10.0f,right/2000.0f);}
 }
 stream_unlock(b);pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_dsb_volume(uint64_t b,uint32_t o,uint32_t p){return control(b,o,p,0,0);}
uint32_t tribes_dsb_pan(uint64_t b,uint32_t o,uint32_t p){return control(b,o,p,1,0);}
uint32_t tribes_dsb_frequency(uint64_t b,uint32_t o,uint32_t p){return control(b,o,p,2,0);}
uint32_t tribes_dsb_set_volume(uint64_t b,uint32_t o,uint32_t p){return control(b,o,p,0,1);}
uint32_t tribes_dsb_set_pan(uint64_t b,uint32_t o,uint32_t p){return control(b,o,p,1,1);}
uint32_t tribes_dsb_set_frequency(uint64_t b,uint32_t o,uint32_t p){return control(b,o,p,2,1);}

uint32_t tribes_dsound_enumerate(_cpu *cpu,uint32_t callback,uint32_t context){
 if(!callback)return INVALID;(void)tribes_lookup(callback);if(!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO))return NODRIVER;
 int count=0;SDL_AudioDeviceID *ids=SDL_GetAudioPlaybackDevices(&count);if(!ids)return GENERIC;if(count<0||count>4096)abort();if(!count){SDL_free(ids);return 0;}
 /* Snapshot native names before callbacks, which can reenter this provider. */
 typedef struct {unsigned char guid[16];char name[256];} Row;
 Row *rows=calloc((size_t)count+1,sizeof *rows);if(!rows){SDL_free(ids);return OOM;}
 strcpy(rows[0].name,"Primary Sound Driver");
 for(int i=0;i<count;i++){memcpy(rows[i+1].guid,&ids[i],4);memcpy(rows[i+1].guid+4,device_tail,12);const char *name=SDL_GetAudioDeviceName(ids[i]);if(!name){free(rows);SDL_free(ids);return GENERIC;}size_t n=strnlen(name,256);if(n==256)abort();for(size_t j=0;j<n;j++)if((unsigned char)name[j]>127){fputs("TribesPad: unsupported DirectSound ANSI device name conversion\n",stderr);abort();}memcpy(rows[i+1].name,name,n+1);}
 SDL_free(ids);if(cpu->_esp<320)abort();uint32_t scratch=cpu->_esp-304;uint64_t base=cpu->_pointer_offset;
 if(cpu->stack_bottom&&base+scratch-20<(uintptr_t)cpu->stack_bottom)abort();
 if(cpu->stack_top&&base+scratch+304>(uintptr_t)cpu->stack_top)abort();
 for(int i=0;i<=count;i++){memcpy(ptr(base,scratch,272),&rows[i],272);memcpy(ptr(base,scratch+272,11),"dsound.dll",11);uint32_t args[]={i?scratch:0,scratch+16,scratch+272,context};if(!tribes_call_guest(cpu,callback,4,args,scratch))break;}
 free(rows);return 0;
}
_Noreturn void tribes_ds_stop(_cpu *cpu,uint32_t method){
 uint32_t args[5]={0};unsigned n=method==3?5:method==5?4:3;memcpy(args,ptr(cpu->_pointer_offset,cpu->_esp,n*4),n*4);
 fprintf(stderr,"TribesPad: unsupported DirectSound method %u return=0x%08x args=%08x,%08x,%08x,%08x\n",method,args[0],args[1],args[2],args[3],args[4]);abort();
}

#ifdef TB_DSOUND_TEST
/* Native test observer attaches to the real logical device postmix. Never
   exported to the original guest or included in production builds. */
SDL_AudioDeviceID tribes_dsound_test_device(uint64_t base,uint32_t object){pthread_mutex_lock(&lock);SDL_AudioDeviceID id=find(base,object)->native;pthread_mutex_unlock(&lock);return id;}
#endif

/* A primary's listener is a real COM interface with the primary's lifetime.
   Parameters belong to its device. Getters expose requested settings; applied
   settings change per immediate field or atomically on CommitDeferredSettings.
   No secondary 3D voice is admitted yet; ordinary PCM sources ignore listener
   transforms. A 3D source must consume listener_active when implemented. */
static Device *listener_find(uint64_t base,uint32_t object){
 Buffer *b=buffer_find(base,object);if(!b->listener||object!=b->listener){fputs("TribesPad: invalid DirectSound listener interface\n",stderr);abort();}return b->owner;
}
static float listener_float(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static int listener_valid(const uint32_t *state){
 if(state[0]!=64)return 0;
 for(unsigned i=1;i<16;i++)if(!isfinite(listener_float(state[i])))return 0;
 if(listener_float(state[13])<=0||listener_float(state[14])<0||listener_float(state[14])>10||listener_float(state[15])<0||listener_float(state[15])>10)return 0;
 double f[3],t[3];for(unsigned i=0;i<3;i++){f[i]=listener_float(state[7+i]);t[i]=listener_float(state[10+i]);}
 double x=f[1]*t[2]-f[2]*t[1],y=f[2]*t[0]-f[0]*t[2],z=f[0]*t[1]-f[1]*t[0];return x!=0||y!=0||z!=0;
}
static uint32_t listener_get(uint64_t base,uint32_t object,uint32_t address,unsigned first,unsigned count){
 void *out=ptr(base,address,count*4);if(!out)return INVALID;
 if(!first){uint32_t size;memcpy(&size,out,4);if(size<64)return INVALID;}
 pthread_mutex_lock(&lock);Device *d=listener_find(base,object);memcpy(out,d->listener_state+first,count*4);pthread_mutex_unlock(&lock);return 0;
}
static uint32_t listener_set(uint64_t base,uint32_t object,const uint32_t *values,unsigned first,unsigned count,uint32_t apply){
 if(apply>1)return INVALID;pthread_mutex_lock(&lock);Device *d=listener_find(base,object);uint32_t next[16];memcpy(next,d->listener_state,64);memcpy(next+first,values,count*4);
 if(!listener_valid(next)){pthread_mutex_unlock(&lock);return INVALID;}
 memcpy(d->listener_state,next,64);if(!apply)memcpy(d->listener_active+first,values,count*4);pthread_mutex_unlock(&lock);return 0;
}
#define LISTENER_GET(name,first,count) uint32_t tribes_dsl_get_##name(uint64_t b,uint32_t o,uint32_t p){return listener_get(b,o,p,first,count);}
LISTENER_GET(all,0,16)
LISTENER_GET(position,1,3)
LISTENER_GET(velocity,4,3)
LISTENER_GET(distance,13,1)
LISTENER_GET(rolloff,14,1)
LISTENER_GET(doppler,15,1)
#undef LISTENER_GET
uint32_t tribes_dsl_get_orientation(uint64_t base,uint32_t object,uint32_t front,uint32_t top){
 void *f=ptr(base,front,12),*t=ptr(base,top,12);if(!f||!t)return INVALID;
 pthread_mutex_lock(&lock);Device *d=listener_find(base,object);memcpy(f,d->listener_state+7,12);memcpy(t,d->listener_state+10,12);pthread_mutex_unlock(&lock);return 0;
}
uint32_t tribes_dsl_set_all(uint64_t base,uint32_t object,uint32_t address,uint32_t apply){
 void *p=ptr(base,address,64);if(!p)return INVALID;uint32_t state[16];memcpy(state,p,64);return listener_set(base,object,state,0,16,apply);
}
#define LISTENER_SCALAR(name,first) uint32_t tribes_dsl_set_##name(uint64_t b,uint32_t o,uint32_t value,uint32_t apply){return listener_set(b,o,&value,first,1,apply);}
LISTENER_SCALAR(distance,13)
LISTENER_SCALAR(rolloff,14)
LISTENER_SCALAR(doppler,15)
#undef LISTENER_SCALAR
#define LISTENER_VECTOR(name,first) uint32_t tribes_dsl_set_##name(uint64_t b,uint32_t o,uint32_t x,uint32_t y,uint32_t z,uint32_t apply){uint32_t v[]={x,y,z};return listener_set(b,o,v,first,3,apply);}
LISTENER_VECTOR(position,1)
LISTENER_VECTOR(velocity,4)
#undef LISTENER_VECTOR
uint32_t tribes_dsl_set_orientation(uint64_t b,uint32_t o,uint32_t x,uint32_t y,uint32_t z,uint32_t tx,uint32_t ty,uint32_t tz,uint32_t apply){uint32_t v[]={x,y,z,tx,ty,tz};return listener_set(b,o,v,7,6,apply);}
uint32_t tribes_dsl_commit(uint64_t base,uint32_t object){pthread_mutex_lock(&lock);Device *d=listener_find(base,object);memcpy(d->listener_active,d->listener_state,64);pthread_mutex_unlock(&lock);return 0;}
#ifdef TB_DSOUND_TEST
void tribes_dsound_test_listener_active(uint64_t base,uint32_t object,uint32_t *out){pthread_mutex_lock(&lock);Device *d=listener_find(base,object);memcpy(out,d->listener_active,64);pthread_mutex_unlock(&lock);}
#endif
