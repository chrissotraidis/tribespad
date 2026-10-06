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
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t);
extern unsigned sdl_test_allocations,sdl_test_frees,sdl_test_live,sdl_test_fail;
#ifdef CHECKED
extern void bad_keyboard(void),bad_gamepad(void);
#endif
static unsigned char *memory;static _cpu cpu;
static uint32_t call(unsigned id,uint32_t arg){
 cpu._esp=0xf0000;memcpy(memory+cpu._esp,&arg,4);_cpu before=cpu;
 tribes_set_last_error(0x19a5);tribes_enter(&cpu,id);
 if(id==8||id==17||id==19||id==35||id==36)assert(cpu._eax==before._eax);
 before._eax=cpu._eax;assert(!memcmp(&cpu,&before,sizeof cpu));
 assert(!memcmp(memory+cpu._esp,&arg,4)&&tribes_get_last_error()==0x19a5);return cpu._eax;
}
static int count(void){int value;memcpy(&value,memory+0x3000,4);return value;}
static void *wrong_thread(void *unused){(void)unused;call(30,0);return NULL;}
static void compare_gamepads(void){
 int n=0;SDL_JoystickID *native=SDL_GetGamepads(&n);assert(native);
 uint32_t copy=call(31,0x3000);assert(copy&&count()==n);
 assert(!memcmp(memory+copy,native,((size_t)n+1)*4));SDL_free(native);
 /* Caller-owned data can be changed and released by ordinary SDL_free. */
 memset(memory+copy,0x5a,((size_t)n+1)*4);call(36,copy);
}
static int input_main(int argc,char **argv){
 assert(pthread_main_np());
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));memset(&cpu,0,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;
 cpu._esp=0xf0000;cpu.stack_bottom=memory+0x10000;cpu.stack_top=memory+0x100000;cpu._eflags=0x246;
 assert(tribes_memory_checks_configure(cpu._pointer_offset,UINT64_C(0x100000000)));
#ifdef CHECKED
 tribes_memory_external_configure(cpu._pointer_offset,UINT64_C(0x100000000));tribes_memory_external_enable();
 if(argc==2&&!strcmp(argv[1],"bad-keyboard")){bad_keyboard();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-gamepad")){bad_gamepad();return 99;}
#endif
 static const char env[]="TRIBESPAD_INPUT_TEST=1\0";assert(tribes_environment_configure(cpu._pointer_offset,env,sizeof env));
 assert(tribes_sdl_set_memory_functions(cpu._pointer_offset,11,12,13,14));call(8,0);
 assert(call(18,SDL_INIT_VIDEO|SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC));
 assert((call(10,0)&0x3200)==0x3200);
 if(argc==2&&!strcmp(argv[1],"wrong-thread")){pthread_t t;assert(!pthread_create(&t,NULL,wrong_thread,NULL));assert(!pthread_join(t,NULL));return 99;}
 if(argc==2&&!strcmp(argv[1],"readonly-count")){assert(tribes_memory_permissions_set(0x3000,4096,1));call(30,0x3000);return 99;}
 sdl_test_fail=UINT32_MAX;assert(!call(30,0x3000)&&count()==0);sdl_test_fail=0;
 uint32_t keyboard=call(30,0x3000);int keys=0;const bool *native_keys=SDL_GetKeyboardState(&keys);
 assert(keyboard&&keys==count()&&!memcmp(memory+keyboard,native_keys,(size_t)keys));
 if(argc==2&&!strcmp(argv[1],"mutated-keyboard")){memory[keyboard]^=1;call(30,0);return 99;}
 assert(call(30,0)==keyboard);assert(call(32,0)==(uint32_t)SDL_HasJoystick());compare_gamepads();
 sdl_test_fail=UINT32_MAX;assert(!call(31,0x3000)&&count()==0&&!strcmp(SDL_GetError(),"Out of memory"));sdl_test_fail=0;
 assert(!call(33,UINT32_MAX)&&*SDL_GetError());assert(!call(34,0)&&*SDL_GetError());call(35,0);
 SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
 desc.name="TribesPad native virtual controller";desc.naxes=6;desc.nbuttons=15;desc.axis_mask=0x3f;desc.button_mask=0x7fff;
 SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);assert(id&&SDL_IsGamepad(id));assert(call(32,0));compare_gamepads();
 uint32_t handle=call(33,id);assert((handle&0xff000000)==0xd9000000);assert(call(33,id)==handle);
 sdl_test_fail=UINT32_MAX;assert(!call(34,handle));sdl_test_fail=0;
 uint32_t name=call(34,handle);assert(name&&!strcmp((char *)memory+name,desc.name));assert(call(34,handle)==name);
 if(argc==2&&!strcmp(argv[1],"mutated-name")){memory[name]^=1;call(34,handle);return 99;}
 if(argc==2&&!strcmp(argv[1],"wrong-token")){call(34,0xd8000001);return 99;}
 call(35,handle);assert(call(34,handle)==name);assert(SDL_DetachVirtualJoystick(id));compare_gamepads();
 assert(call(34,handle)==name);call(35,handle);assert(!tribes_global_size(cpu._pointer_offset,name));
 if(argc==2&&!strcmp(argv[1],"stale-token")){call(34,handle);return 99;}
 id=SDL_AttachVirtualJoystick(&desc);assert(id);uint32_t newer=call(33,id);assert(newer&&newer!=handle&&call(34,newer));
 assert(call(18,SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC));call(17,SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC);
 assert(call(34,newer));call(17,SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC);
 assert(!call(10,SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC));
 assert(call(18,SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC));compare_gamepads();call(19,0);assert(!sdl_test_live);
 assert(call(18,SDL_INIT_VIDEO|SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC));assert(call(30,0));compare_gamepads();call(19,0);assert(!sdl_test_live);
 printf("PASS: real native input initialization, keyboard copy, enumeration/OOM/free, virtual controller open/name/refcounts/detach/quit/reinit, CPU/LastError; %u allocations, %u frees, %u live\n",sdl_test_allocations,sdl_test_frees,sdl_test_live);fflush(stdout);return 0;
}
int main(int argc,char **argv){return tribes_sdl_run_application(argc,argv,input_main);}
