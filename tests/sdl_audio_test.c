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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <TargetConditionals.h>
extern void tribes_enter(_cpu *,uint32_t);
extern unsigned sdl_test_allocations,sdl_test_frees,sdl_test_live,sdl_test_fail;
static unsigned char *memory;static _cpu cpu;
static uint32_t call(unsigned id,uint32_t value,uint32_t second){
 uint32_t args[]={value,second};unsigned n=(id==8||id==19)?0:(id==1||id==7)?2:1;
 cpu._esp=0xf0000;memcpy(memory+cpu._esp,args,n*4);_cpu before=cpu;
 tribes_set_last_error(0x19a5);tribes_enter(&cpu,id);
 if(id==8||id==17||id==19||id==7)assert(before._eax==cpu._eax);before._eax=cpu._eax;
 assert(!memcmp(&before,&cpu,sizeof cpu)&&!memcmp(memory+cpu._esp,args,n*4)&&tribes_get_last_error()==0x19a5);
 return cpu._eax;
}
extern unsigned sdl_test_log_count;
extern char sdl_test_log_message[4096];
static void *emit_worker_log(void *unused){(void)unused;SDL_LogMessage(19,SDL_LOG_PRIORITY_ERROR,"%s","audio worker log");return NULL;}
static void *wrong_thread(void *ignored){(void)ignored;call(18,SDL_INIT_VIDEO,0);return NULL;}
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
 SDL_AudioSpec spec={SDL_AUDIO_F32,2,48000};
 SDL_AudioDeviceID device=SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec);assert(device);
 SDL_AudioStream *stream=SDL_CreateAudioStream(&spec,&spec);assert(stream);
 float silence[4800]={0};assert(SDL_PutAudioStreamData(stream,silence,sizeof silence));assert(SDL_GetAudioStreamQueued(stream)==sizeof silence);
 assert(SDL_BindAudioStream(device,stream));
 for(unsigned i=0;i<200&&SDL_GetAudioStreamQueued(stream);i++)SDL_Delay(10);
 assert(!SDL_GetAudioStreamQueued(stream));
 call(7,15,0x2468);unsigned count=sdl_test_log_count;pthread_t logger;assert(!pthread_create(&logger,NULL,emit_worker_log,NULL));assert(!pthread_join(logger,NULL));assert(sdl_test_log_count==count);
 call(10,0,0);assert(sdl_test_log_count==count+1&&!strcmp(sdl_test_log_message,"audio worker log"));call(7,0,0);
 SDL_DestroyAudioStream(stream);SDL_CloseAudioDevice(device);call(17,SDL_INIT_AUDIO,0);assert(!call(10,SDL_INIT_AUDIO,0));

 if(argc==2&&!strcmp(argv[1],"unclean-return"))return 99;
 assert(call(18,SDL_INIT_VIDEO,0));call(17,SDL_INIT_VIDEO,0);assert(call(10,SDL_INIT_VIDEO,0)==SDL_INIT_VIDEO);
 call(17,SDL_INIT_VIDEO,0);assert(call(10,0,0)==SDL_INIT_EVENTS);
 assert(call(18,SDL_INIT_VIDEO,0));assert(SDL_GetPrimaryDisplay());
 call(19,0,0);assert(!sdl_test_live);assert(call(10,0,0)==0);
 assert(call(18,SDL_INIT_VIDEO,0));assert(SDL_GetPrimaryDisplay());call(19,0,0);assert(!sdl_test_live);
 if(argc==2&&!strcmp(argv[1],"after-finish")){tribes_sdl_host_application_finished();call(10,0,0);return 99;}
 printf("PASS: native %s video and CoreAudio silent playback, queued worker logs via Apple application main; display=%u; failed-init retry, subsystem refs/quit/full quit/reinit, guest cdecl CPU/LastError and allocator; %u allocations, %u frees, %u live\n",driver,display,sdl_test_allocations,sdl_test_frees,sdl_test_live);fflush(stdout);return 0;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"no-host-entry"))return video_main(argc,argv);
 return tribes_sdl_run_application(argc,argv,video_main);
}
