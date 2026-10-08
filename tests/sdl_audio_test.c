#include "../port/apple/sdl_main.h"
#include "../port/core/guest_sdl_backend.h"
#include "../port/core/guest_sdl_memory.h"
#include "../port/core/guest_heap.h"
#include "../port/core/process_environment.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <TargetConditionals.h>
extern void tribes_enter(_cpu *,uint32_t);
extern unsigned sdl_test_allocations,sdl_test_frees,sdl_test_live,sdl_test_fail;
static unsigned char *memory;static _cpu cpu;
static uint32_t call_cpu(_cpu *state,unsigned id,unsigned n,const uint32_t *args){
 memcpy(memory+state->_esp,args,n*4);_cpu before=*state;
 tribes_set_last_error(0x19a5);tribes_enter(state,id);
 if(id==8||id==17||id==19||id==7||id==25||id==28)assert(before._eax==state->_eax);before._eax=state->_eax;
 assert(!memcmp(&before,state,sizeof *state)&&!memcmp(memory+state->_esp,args,n*4)&&tribes_get_last_error()==0x19a5);
 return state->_eax;
}
static uint32_t call_args(unsigned id,unsigned n,const uint32_t *args){
 cpu._esp=0xf0000;return call_cpu(&cpu,id,n,args);
}
static uint32_t call(unsigned id,uint32_t value,uint32_t second){
 uint32_t args[]={value,second};unsigned n=(id==8||id==19)?0:(id==1||id==7||id==22||id==26||id==27)?2:1;
 return call_args(id,n,args);
}
#define THREE(id,a,b,c) call_args(id,3,(uint32_t[]){a,b,c})
extern SDL_AudioStream *sdl_test_audio_stream;
extern unsigned sdl_test_log_count;
extern char sdl_test_log_message[4096];
static void *emit_worker_log(void *unused){(void)unused;SDL_LogMessage(19,SDL_LOG_PRIORITY_ERROR,"%s","audio worker log");return NULL;}
static void *wrong_thread(void *ignored){(void)ignored;call(18,SDL_INIT_VIDEO,0);return NULL;}
typedef struct { _cpu cpu;uint32_t stream;atomic_int done;int stale; } AudioWorker;
static void *stream_worker(void *opaque){
 AudioWorker *w=opaque;uint32_t token=w->stream;
 #define WORK(id,n,...) call_cpu(&w->cpu,id,n,(uint32_t[]){__VA_ARGS__})
 assert(WORK(32,1,token)==0);
 if(w->stale)return NULL; /* The stale query above must abort. */
 int16_t samples[256];for(unsigned i=0;i<256;i++)samples[i]=(int16_t)(i*97-12000);
 memcpy(memory+0x20000,samples,sizeof samples);
 for(unsigned i=0;i<128;i++){
  assert(WORK(34,3,token,0x20000,sizeof samples));assert(WORK(35,1,token));
  assert(WORK(32,1,token)==sizeof samples);
  memset(memory+0x22000,0x5a,sizeof samples+16);
  assert(WORK(33,3,token,0x22000,sizeof samples+16)==sizeof samples);
  assert(!memcmp(memory+0x22000,samples,sizeof samples));
  for(unsigned j=sizeof samples;j<sizeof samples+16;j++)assert(memory[0x22000+j]==0x5a);
  assert(WORK(31,1,token));
 }
 /* Original decoder cleanup can also run on its worker. */
 WORK(28,1,token);atomic_store(&w->done,1);
 #undef WORK
 return NULL;
}
static void worker_roundtrip(int stale){
 SDL_AudioSpec spec={SDL_AUDIO_S16LE,1,8000};memcpy(memory+0x3200,&spec,12);
 uint32_t token=call(26,0x3200,0x3200);assert(token);
 AudioWorker w={.cpu=cpu,.stream=token,.stale=stale};w.cpu._esp=0xe0000;atomic_init(&w.done,0);
 if(stale)call(28,token,0);
 pthread_t t;assert(!pthread_create(&t,NULL,stream_worker,&w));
 /* Concurrent main-thread audio calls exercise the shared registry lock;
    never query a token after its worker has destroyed it. */
 while(!atomic_load(&w.done)&&!stale){assert(call(32,0,0)==UINT32_MAX);SDL_Delay(1);}
 assert(!pthread_join(t,NULL));
}
static int video_main(int argc,char **argv){
 assert(pthread_main_np());
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));memset(&cpu,0,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;
 cpu._esp=0xf0000;cpu.stack_bottom=memory+0x10000;cpu.stack_top=memory+0x100000;cpu._eflags=0x246;
 assert(tribes_memory_checks_configure(cpu._pointer_offset,UINT64_C(0x100000000)));
#ifdef CHECKED
 tribes_memory_external_configure(cpu._pointer_offset,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 static const char env[]="TRIBESPAD_VIDEO_TEST=1\0";assert(tribes_environment_configure(cpu._pointer_offset,env,sizeof env));
 assert(tribes_sdl_set_memory_functions(cpu._pointer_offset,11,12,13,14));
 const char *driver=TARGET_OS_OSX?"cocoa":"uikit";
 strcpy((char *)memory+0x1000,SDL_HINT_VIDEO_DRIVER);strcpy((char *)memory+0x2000,driver);assert(call(1,0x1000,0x2000));
 call(8,0,0);call(7,0,0);assert(call(9,SDL_INIT_EVENTS,0));
 if(argc==2&&!strcmp(argv[1],"wrong-thread")){pthread_t t;assert(!pthread_create(&t,NULL,wrong_thread,NULL));assert(!pthread_join(t,NULL));return 99;}
 strcpy((char *)memory+0x2000,"tribespad-invalid-driver");assert(call(1,0x1000,0x2000));assert(!call(18,SDL_INIT_VIDEO,0));
 strcpy((char *)memory+0x2000,driver);assert(call(1,0x1000,0x2000));
 assert(call(10,0,0)==SDL_INIT_EVENTS);
 assert(call(18,SDL_INIT_VIDEO,0));assert(call(10,0,0)==(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
 /* These native read-only getters do not allocate or expose pointers to the
    guest. They establish that the selected platform driver enumerated a real
    display, rather than a dummy/headless substitute. */
 assert(SDL_GetCurrentVideoDriver()&&!strcmp(SDL_GetCurrentVideoDriver(),driver));
 SDL_DisplayID display=SDL_GetPrimaryDisplay();assert(display);
 strcpy((char *)memory+0x1200,SDL_HINT_AUDIO_DRIVER);strcpy((char *)memory+0x2200,"tribespad-invalid-audio-driver");assert(call(1,0x1200,0x2200));assert(!call(18,SDL_INIT_AUDIO,0));assert(!call(10,SDL_INIT_AUDIO,0));
 strcpy((char *)memory+0x2200,"directsound");assert(call(1,0x1200,0x2200));assert(!strcmp(SDL_GetHint(SDL_HINT_AUDIO_DRIVER),"coreaudio"));
 assert(call(18,SDL_INIT_AUDIO,0));assert(!strcmp(SDL_GetCurrentAudioDriver(),"coreaudio"));
 assert(call(18,SDL_INIT_AUDIO,0));call(17,SDL_INIT_AUDIO,0);assert(call(10,SDL_INIT_AUDIO,0)==SDL_INIT_AUDIO);
 /* Exercise the original NULL-spec device and paused/bound stream sequence
    through emitted guest cdecl wrappers, against actual CoreAudio devices. */
 SDL_AudioDeviceID device=call(22,SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,0);assert(device);
 assert(call(24,device,0)&&SDL_AudioDevicePaused(device));
 SDL_AudioSpec expected;int frames;assert(SDL_GetAudioDeviceFormat(device,&expected,&frames));
 memset(memory+0x3000,0xa5,32);assert(THREE(23,device,0x3000,0x3010));
 assert(!memcmp(memory+0x3000,&expected,12)&&*(int *)(memory+0x3010)==frames);
 assert(memory[0x300c]==0xa5&&memory[0x3014]==0xa5);
 SDL_AudioSpec destination={SDL_AUDIO_S16LE,1,8000};memcpy(memory+0x3100,&destination,12);
 uint32_t stream=call(26,0x3000,0x3100);assert(stream>=0xd7000001&&stream<0xd8000000);
 assert(call(27,device,stream));assert(call(29,stream,0)&&!SDL_AudioDevicePaused(device));
 assert(call(30,stream,0)&&SDL_AudioDevicePaused(device));assert(call(31,stream,0));
 assert(call(32,stream,0)==0);memset(memory+0x4000,0x5a,64);assert(!THREE(33,stream,0x4000,64));
 for(unsigned i=0;i<64;i++)assert(memory[0x4000+i]==0x5a);
 assert((int32_t)THREE(33,stream,0,64)==-1);assert((int32_t)THREE(33,stream,0x4000,UINT32_MAX)==-1);
 assert(!THREE(33,stream,0x4000,0));
 if(argc==2&&!strcmp(argv[1],"foreign-stream")){call(32,0xd700ffff,0);return 99;}
 if(argc==2&&!strcmp(argv[1],"stale-stream")){call(28,stream,0);call(32,stream,0);return 99;}
 if(argc==2&&!strcmp(argv[1],"stale-device")){call(25,device,0);call(24,device,0);return 99;}
 if(argc==2&&!strcmp(argv[1],"unreadable-spec")){assert(tribes_memory_permissions_set(0x60000,0x1000,0));call(26,0x60000,0x3100);return 99;}
 if(argc==2&&!strcmp(argv[1],"readonly-format")){assert(tribes_memory_permissions_set(0x60000,0x1000,1));THREE(23,device,0x60000,0);return 99;}
 /* Exercise copied guest PCM through both directions of the native stream. */
 call(28,stream,0);
 SDL_AudioSpec src={SDL_AUDIO_S16LE,1,8000},dst={SDL_AUDIO_F32LE,1,8000};
 memcpy(memory+0x3000,&src,12);memcpy(memory+0x3100,&dst,12);
 uint32_t converted=call(26,0x3000,0x3100);assert(converted!=stream);
 const int16_t samples[]={-32768,-16384,0,16384};float values[]={-1,-.5f,0,.5f};
 memcpy(memory+0x5001,samples,sizeof samples);
 if(argc==2&&!strcmp(argv[1],"unreadable-pcm")){assert(tribes_memory_permissions_set(0x5000,0x1000,0));THREE(34,converted,0x5001,sizeof samples);return 99;}
 if(argc==2&&!strcmp(argv[1],"overflow-pcm")){THREE(34,converted,0xfffffffc,sizeof samples);return 99;}
 assert(!THREE(34,converted,0,8));assert(!THREE(34,converted,0x5001,UINT32_MAX));assert(THREE(34,converted,0x5001,0));
 assert(THREE(34,converted,0x5001,sizeof samples));memset(memory+0x5001,0,sizeof samples);
 assert(call(35,converted,0));assert(call(32,converted,0)==sizeof values);
 if(argc==2&&!strcmp(argv[1],"readonly-data")){assert(tribes_memory_permissions_set(0x60000,0x1000,1));THREE(33,converted,0x60000,64);return 99;}
 if(argc==2&&!strcmp(argv[1],"overflow-data")){THREE(33,converted,0xfffffff8,64);return 99;}
 /* A protected tail outside the actual returned extent must stay untouched. */
 assert(tribes_memory_permissions_set(0x80000,0x1000,0));memset(memory+0x7fff0,0x5a,16);
 assert(THREE(33,converted,0x7fff0,64)==sizeof values);assert(!memcmp(memory+0x7fff0,values,sizeof values));
 assert(tribes_memory_permissions_set(0x80000,0x1000,3));
 memcpy(memory+0x5001,samples,sizeof samples);assert(THREE(34,converted,0x5001,sizeof samples));assert(call(35,converted,0));
 memset(memory+0x4000,0x5a,64);assert(THREE(33,converted,0x4000,64)==sizeof values);
 assert(!memcmp(memory+0x4000,values,sizeof values));for(unsigned i=16;i<64;i++)assert(memory[0x4000+i]==0x5a);
 call(28,converted,0);
 worker_roundtrip(argc==2&&!strcmp(argv[1],"stale-worker-stream"));
 /* Actual device consumption, using a guest-owned stream and native callback
    storage; no dummy device or fabricated completion. */
 SDL_AudioSpec spec={SDL_AUDIO_F32,2,48000};memcpy(memory+0x3000,&spec,12);
 stream=call(26,0x3000,0x3000);assert(stream);SDL_AudioStream *native=sdl_test_audio_stream;
 float silence[4800]={0};memcpy(memory+0x9000,silence,sizeof silence);assert(THREE(34,stream,0x9000,sizeof silence));assert(call(35,stream,0));assert(SDL_GetAudioStreamQueued(native)==sizeof silence);
 assert(call(27,device,stream)&&call(29,stream,0));
 for(unsigned i=0;i<200&&SDL_GetAudioStreamQueued(native);i++)SDL_Delay(10);
 assert(!SDL_GetAudioStreamQueued(native));assert(call(30,stream,0));assert(call(31,stream,0));
 call(7,15,0x2468);unsigned count=sdl_test_log_count;pthread_t logger;assert(!pthread_create(&logger,NULL,emit_worker_log,NULL));assert(!pthread_join(logger,NULL));assert(sdl_test_log_count==count);
 call(10,0,0);assert(sdl_test_log_count==count+1&&!strcmp(sdl_test_log_message,"audio worker log"));call(7,0,0);
 call(28,stream,0);call(25,device,0);
 /* Final subsystem shutdown owns destruction of any remaining stream/device;
    reinitialization must reject the old token and retain fresh identities. */
 device=call(22,SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,0);assert(device);stream=call(26,0x3000,0x3000);assert(stream);
 assert(call(18,SDL_INIT_AUDIO,0));call(17,SDL_INIT_AUDIO,0);assert(call(10,SDL_INIT_AUDIO,0));assert(!call(32,stream,0));
 call(17,SDL_INIT_AUDIO,0);assert(!call(10,SDL_INIT_AUDIO,0));
 assert(call(18,SDL_INIT_AUDIO,0));
 if(argc==2&&!strcmp(argv[1],"after-audio-quit")){call(32,stream,0);return 99;}
 uint32_t fresh=call(26,0,0);assert(fresh>stream);call(19,0,0);assert(call(9,SDL_INIT_EVENTS,0));
 assert(call(18,SDL_INIT_VIDEO,0));

 if(argc==2&&!strcmp(argv[1],"unclean-return"))return 99;
 assert(call(18,SDL_INIT_VIDEO,0));call(17,SDL_INIT_VIDEO,0);assert(call(10,SDL_INIT_VIDEO,0)==SDL_INIT_VIDEO);
 call(17,SDL_INIT_VIDEO,0);assert(call(10,0,0)==SDL_INIT_EVENTS);
 assert(call(18,SDL_INIT_VIDEO,0));assert(SDL_GetPrimaryDisplay());
 call(19,0,0);assert(!sdl_test_live);assert(call(10,0,0)==0);
 assert(call(18,SDL_INIT_VIDEO,0));assert(SDL_GetPrimaryDisplay());call(19,0,0);assert(!sdl_test_live);
 if(argc==2&&!strcmp(argv[1],"after-finish")){tribes_sdl_host_application_finished();call(10,0,0);return 99;}
 printf("PASS: native %s video and CoreAudio guest device/stream setup, PCM conversion and bounded output, worker PCM roundtrip and destruction, playback/pause, stale-token and shutdown lifetimes, queued worker logs via Apple application main; display=%u; failed-init retry, subsystem refs/quit/full quit/reinit, guest cdecl CPU/LastError and allocator; %u allocations, %u frees, %u live\n",driver,display,sdl_test_allocations,sdl_test_frees,sdl_test_live);fflush(stdout);return 0;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"no-host-entry"))return video_main(argc,argv);
 return tribes_sdl_run_application(argc,argv,video_main);
}
