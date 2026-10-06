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
_Static_assert(sizeof(SDL_DisplayID)==4&&sizeof(SDL_PixelFormat)==4,"SDL scalar ABI");
extern void tribes_enter(_cpu *,uint32_t);
extern unsigned sdl_test_allocations,sdl_test_frees,sdl_test_live,sdl_test_fail;
#ifdef CHECKED
extern void bad_primary(void),bad_desktop(void),bad_oom(void);
#endif
static unsigned char *memory;static _cpu cpu;
static uint32_t call(unsigned id,uint32_t value,uint32_t second){
 uint32_t args[]={value,second};unsigned n=(id==8||id==19||id==20)?0:(id==1||id==7)?2:1;
 cpu._esp=0xf0000;memcpy(memory+cpu._esp,args,n*4);_cpu before=cpu;
 tribes_set_last_error(0x19a5);tribes_enter(&cpu,id);
 if(id==8||id==17||id==19||id==7)assert(before._eax==cpu._eax);before._eax=cpu._eax;
 assert(!memcmp(&before,&cpu,sizeof cpu)&&!memcmp(memory+cpu._esp,args,n*4)&&tribes_get_last_error()==0x19a5);
 return cpu._eax;
}
static void *wrong_thread(void *ignored){(void)ignored;call(20,0,0);return NULL;}
static int video_main(int argc,char **argv){
 assert(pthread_main_np());
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));memset(&cpu,0,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;
 cpu._esp=0xf0000;cpu.stack_bottom=memory+0x10000;cpu.stack_top=memory+0x100000;cpu._eflags=0x246;
 assert(tribes_memory_checks_configure(cpu._pointer_offset,UINT64_C(0x100000000)));
#ifdef CHECKED
 tribes_memory_external_configure(cpu._pointer_offset,UINT64_C(0x100000000));tribes_memory_external_enable();
 if(argc==2&&!strcmp(argv[1],"bad-primary")){bad_primary();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-desktop")){bad_desktop();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-oom")){bad_oom();return 99;}
#endif
 static const char env[]="TRIBESPAD_VIDEO_TEST=1\0";assert(tribes_environment_configure(cpu._pointer_offset,env,sizeof env));
 assert(tribes_sdl_set_memory_functions(cpu._pointer_offset,11,12,13,14));
 const char *driver=TARGET_OS_OSX?"cocoa":"uikit";
 strcpy((char *)memory+0x1000,SDL_HINT_VIDEO_DRIVER);strcpy((char *)memory+0x2000,driver);assert(call(1,0x1000,0x2000));
 call(8,0,0);call(7,0,0);assert(call(9,SDL_INIT_EVENTS,0));
 if(argc==2&&!strcmp(argv[1],"wrong-thread")){pthread_t t;assert(!pthread_create(&t,NULL,wrong_thread,NULL));assert(!pthread_join(t,NULL));return 99;}
 assert(!call(20,0,0));assert(!call(21,0,0));assert(*SDL_GetError());
 assert(call(18,SDL_INIT_VIDEO,0));assert(call(10,0,0)==(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
 assert(SDL_GetCurrentVideoDriver()&&!strcmp(SDL_GetCurrentVideoDriver(),driver));
 uint32_t display=call(20,0,0);assert(display&&display==SDL_GetPrimaryDisplay());
 sdl_test_fail=UINT32_MAX;assert(!call(21,display,0));assert(!strcmp(SDL_GetError(),"Out of memory"));sdl_test_fail=0;
 uint32_t address=call(21,display,0);assert(address);
 TribesSDLDisplayMode actual;memcpy(&actual,memory+address,sizeof actual);
 const SDL_DisplayMode *native=SDL_GetDesktopDisplayMode(display);assert(native);
 assert(actual.display_id==native->displayID&&actual.format==native->format&&actual.width==native->w&&actual.height==native->h);
 assert(!memcmp(&actual.pixel_density,&native->pixel_density,4)&&!memcmp(&actual.refresh_rate,&native->refresh_rate,4));
 assert(actual.refresh_numerator==native->refresh_rate_numerator&&actual.refresh_denominator==native->refresh_rate_denominator);
 assert((actual.internal!=0)==(native->internal!=NULL));assert(actual.width>0&&actual.height>0);
 _cpu saved=cpu;uint32_t saved_error=tribes_get_last_error();
 assert(tribes_sdl_desktop_dimensions(&cpu,address)==(((uint64_t)(uint32_t)native->h<<32)|(uint32_t)native->w));
 assert(!tribes_sdl_desktop_dimensions(&cpu,address+4)&&!tribes_sdl_desktop_dimensions(&cpu,0));
 assert(!memcmp(&cpu,&saved,sizeof cpu)&&tribes_get_last_error()==saved_error);
 if(argc==2&&!strcmp(argv[1],"snapshot-mutation")){memory[address+8]^=1;tribes_sdl_desktop_dimensions(&cpu,address);return 99;}
 unsigned allocations=sdl_test_allocations;
 for(unsigned i=0;i<32;i++){assert(call(20,0,0)==display);assert(call(21,display,0)==address);assert(!memcmp(memory+address,&actual,sizeof actual));}
 assert(sdl_test_allocations==allocations);
 if(argc==2&&!strcmp(argv[1],"mutated-mode")){memory[address+8]^=1;call(21,display,0);return 99;}
 if(argc==2&&!strcmp(argv[1],"mutated-token")){memory[address+32]^=1;call(21,display,0);return 99;}
 if(argc==2&&!strcmp(argv[1],"readonly-mode")){assert(tribes_memory_permissions_set(address&~4095u,4096,1));call(21,display,0);return 99;}
 if(argc==2&&!strcmp(argv[1],"unreadable-mode")){assert(tribes_memory_permissions_set(address&~4095u,4096,0));call(21,display,0);return 99;}
 assert(!call(21,UINT32_MAX,0)&&*SDL_GetError());assert(call(21,display,0)==address);
 sdl_test_fail=UINT32_MAX;assert(call(21,display,0)==address);sdl_test_fail=0;
 if(argc==2&&!strcmp(argv[1],"unclean-return"))return 99;
 assert(call(18,SDL_INIT_VIDEO,0));call(17,SDL_INIT_VIDEO,0);assert(call(10,SDL_INIT_VIDEO,0)==SDL_INIT_VIDEO);assert(call(21,display,0)==address);
 call(17,SDL_INIT_VIDEO,0);assert(call(10,0,0)==SDL_INIT_EVENTS);assert(!tribes_global_size(cpu._pointer_offset,address));assert(!tribes_sdl_desktop_dimensions(&cpu,address));
 assert(!call(20,0,0)&&!call(21,display,0));
 assert(call(18,SDL_INIT_VIDEO,0));display=call(20,0,0);assert(display);address=call(21,display,0);assert(address);
 call(19,0,0);assert(!sdl_test_live);assert(call(10,0,0)==0);
 assert(call(18,SDL_INIT_VIDEO,0));display=call(20,0,0);assert(display&&call(21,display,0));call(19,0,0);assert(!sdl_test_live);
 if(argc==2&&!strcmp(argv[1],"after-finish")){tribes_sdl_host_application_finished();call(20,0,0);return 99;}
 printf("PASS: native %s primary/desktop display via guest ABI; display=%u; exact32-bit fields, cached lifetime, OOM/retry, invalid IDs, subsystem refs/quit/reinit, CPU/LastError; %u allocations, %u frees, %u live\n",driver,display,sdl_test_allocations,sdl_test_frees,sdl_test_live);fflush(stdout);return 0;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"no-host-entry"))return video_main(argc,argv);
 return tribes_sdl_run_application(argc,argv,video_main);
}
