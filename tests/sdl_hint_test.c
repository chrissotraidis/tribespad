#include "../port/core/guest_sdl_backend.h"
#include "../port/core/guest_sdl_memory.h"
#include "../port/core/guest_heap.h"
#include "../port/core/process_environment.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
extern unsigned sdl_test_allocations,sdl_test_frees,sdl_test_live,sdl_test_fail;
extern uint32_t sdl_test_latest;
extern unsigned sdl_test_log_emit,sdl_test_log_count;
extern uint32_t sdl_test_log_userdata,sdl_test_log_category,sdl_test_log_priority;
extern char sdl_test_log_message[4096];
extern char sdl_test_metadata[4][128];extern unsigned sdl_test_metadata_present[4];
static unsigned char *memory;
static _cpu cpu;
static uint32_t call(unsigned id,uint32_t name,uint32_t value,uint32_t priority){
 uint32_t args[]={name,value,priority};unsigned n=(id==8||id==19)?0:(id==1||id==6||id==7)?2:(id==2||id==5)?3:1;
 cpu._esp=0xf0000;memcpy(memory+cpu._esp,args,n*4);_cpu before=cpu;
 tribes_set_last_error(0x12345678);tribes_enter(&cpu,id);if(id==7||id==8||id==17||id==19)assert(before._eax==cpu._eax);before._eax=cpu._eax;
 assert(!memcmp(&before,&cpu,sizeof cpu));assert(!memcmp(memory+cpu._esp,args,n*4));assert(tribes_get_last_error()==0x12345678);return cpu._eax;
}
static void text(uint32_t p,const char *s){strcpy((char *)memory+p,s);}
static const char *hint(const char *name){text(0x3000,name);uint32_t p=call(4,0x3000,0,0);return p?(const char *)memory+p:NULL;}
#ifdef CHECKED_TEST
extern void sdl_test_guest_pointer(void *),sdl_test_wrong_abi(void),sdl_test_bad_callback(void *),sdl_test_metadata_guest_pointer(void *),sdl_test_metadata_wrong_abi(void);
extern void sdl_test_log_bad_callback(void *),sdl_test_log_guest_userdata(void *),sdl_test_log_guest_message(void *),sdl_test_log_bad_format(void),sdl_test_log_wrong_abi(void),sdl_test_log_wrong_varargs(void);
extern void sdl_test_init_wrong_abi(void),sdl_test_was_init_wrong_abi(void),sdl_test_quit_subsystem_wrong_abi(void);
static void *wrong_thread(void *unused){(void)unused;tribes_sdl_set_hint(&cpu,0x1000,0x2000);return NULL;}
static void *wrong_log_thread(void *unused){(void)unused;SDL_LogMessage(19,SDL_LOG_PRIORITY_ERROR,"%s","wrong thread");return NULL;}
static void fault(unsigned mode){
 pid_t child=fork();assert(child>=0);if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);
  if(mode==0){assert(tribes_memory_permissions_set(0x1000,4096,0));call(1,0x1000,0x2000,0);}
  if(mode==1)sdl_test_guest_pointer(memory+0x1000);
  if(mode==2)sdl_test_wrong_abi();
  if(mode==3)sdl_test_bad_callback(memory+0x1000);
  if(mode==4){memory[sdl_test_latest]^=1;call(1,0x1000,0x2000,0);}
  if(mode==5){pthread_t t;assert(!pthread_create(&t,NULL,wrong_thread,NULL));pthread_join(t,NULL);}
  if(mode==6){assert(tribes_sdl_set_memory_functions(cpu._pointer_offset,12,12,13,14));call(1,0x1000,0x2000,0);}
  if(mode==7){assert(tribes_memory_permissions_set(0x4000,4096,0));call(5,0x1000,0x2000,0x4000);}
  if(mode==8)sdl_test_metadata_guest_pointer(memory+0x1000);
  if(mode==9)sdl_test_metadata_wrong_abi();
  if(mode==10)SDL_LogMessage(19,SDL_LOG_PRIORITY_ERROR,"%s","no active frame");
  if(mode==11){pthread_t t;assert(!pthread_create(&t,NULL,wrong_log_thread,NULL));pthread_join(t,NULL);}
  if(mode==12)sdl_test_log_bad_callback(memory+0x1000);
  if(mode==13)sdl_test_log_guest_userdata(memory+0x1000);
  if(mode==14)sdl_test_log_guest_message(memory+0x1000);
  if(mode==15)sdl_test_log_bad_format();
  if(mode==16)sdl_test_log_wrong_abi();
  if(mode==17)sdl_test_log_wrong_varargs();
  if(mode==18)call(9,SDL_INIT_CAMERA,0,0);
  if(mode==19)sdl_test_init_wrong_abi();
  if(mode==20)sdl_test_was_init_wrong_abi();
  if(mode==21)sdl_test_quit_subsystem_wrong_abi();
  _exit(99);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
#endif
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));memset(&cpu,0,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;
 cpu._esp=0xf0000;cpu.stack_bottom=memory+0x10000;cpu.stack_top=memory+0x100000;cpu._eflags=0x246;
 assert(tribes_memory_checks_configure(cpu._pointer_offset,UINT64_C(0x100000000)));
#if defined(CHECKED_TEST) || defined(CHECKED_GUARDS)
 tribes_memory_external_configure(cpu._pointer_offset,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 static const char env[]="SDL_TEST_ENV=guest\0SDL_TEST_UNICODE=caf\x82\0";
 assert(tribes_environment_configure(cpu._pointer_offset,env,sizeof env));
 assert(tribes_sdl_set_memory_functions(cpu._pointer_offset,11,12,13,14));
 assert(!setenv("TRIBESPAD_SDL_HOST_SENTINEL","must-stay-host-only",1));
 text(0x1000,"SDL_TIMER_RESOLUTION");text(0x2000,"1");assert(call(1,0x1000,0x2000,0));
 const char *value=hint("SDL_TIMER_RESOLUTION");assert(value&&!strcmp(value,"1"));
 assert((uintptr_t)value>=cpu._pointer_offset&&(uintptr_t)value<cpu._pointer_offset+UINT64_C(0x100000000));
 assert(!hint("TRIBESPAD_SDL_HOST_SENTINEL"));assert(!strcmp(hint("SDL_TEST_UNICODE"),"caf\xc3\xa9"));
 text(0x2000,"changed input");assert(!strcmp(hint("SDL_TIMER_RESOLUTION"),"1"));
 text(0x1000,"SDL_TEST_ENV");assert(!call(1,0x1000,0x2000,0));assert(!strcmp(hint("SDL_TEST_ENV"),"guest"));
 assert(call(2,0x1000,0x2000,SDL_HINT_OVERRIDE));assert(!strcmp(hint("SDL_TEST_ENV"),"changed input"));
 assert(!call(2,0x1000,0x2000,SDL_HINT_DEFAULT));assert(call(3,0x1000,0,0));assert(!strcmp(hint("SDL_TEST_ENV"),"guest"));
 text(0x1000,"SDL_TEST_LOCAL");
 text(0x2000,"");assert(call(1,0x1000,0x2000,0));const char *empty=hint("SDL_TEST_LOCAL");assert(empty&&!*empty);
 for(unsigned i=0;i<128;i++){char v[32];snprintf(v,sizeof v,"value-%u",i);text(0x2000,v);assert(call(1,0x1000,0x2000,0));assert(!strcmp(hint("SDL_TEST_LOCAL"),v));}
 assert(call(1,0x1000,0,0));assert(!hint("SDL_TEST_LOCAL"));assert(call(3,0x1000,0,0));
 assert(!strcmp(value,"1")); /* Earlier nonempty GetHint results persist. */
 assert(!call(1,0,0x2000,0));text(0x1000,"");assert(!call(1,0x1000,0x2000,0));
 text(0x1000,"SDL_TEST_OOM");text(0x2000,"uncached allocation failure value");assert(call(1,0x1000,0x2000,0));sdl_test_fail=1;assert(!hint("SDL_TEST_OOM"));
 assert(call(1,0x1000,0x2000,0));
#ifdef CHECKED_TEST
 for(unsigned i=0;i<10;i++)fault(i);
#endif
 text(0x1000,"Tribes2.net");text(0x2000,"20250922");text(0x4000,"net.tribes2.next");
 assert(call(5,0x1000,0x2000,0x4000));
 assert(!strcmp(sdl_test_metadata[0],"Tribes2.net"));
 assert(!strcmp(sdl_test_metadata[1],"20250922"));
 assert(!strcmp(sdl_test_metadata[2],"net.tribes2.next"));
 text(0x1000,"changed caller buffer");text(0x5000,SDL_PROP_APP_METADATA_TYPE_STRING);text(0x6000,"game");assert(call(6,0x5000,0x6000,0));assert(!strcmp(sdl_test_metadata[0],"Tribes2.net"));
 text(0x1000,SDL_PROP_APP_METADATA_CREATOR_STRING);text(0x2000,"Créateur");assert(call(6,0x1000,0x2000,0));
 assert(!strcmp(sdl_test_metadata[3],"Créateur"));
 assert(call(6,0x1000,0x2000,0));assert(!strcmp(sdl_test_metadata[3],"Créateur"));
 text(0x1000,"invalid.property");assert(!call(6,0x1000,0x2000,0));assert(!call(6,0,0x2000,0));
 assert(call(5,0,0,0));assert(!strcmp(sdl_test_metadata[0],"SDL Application"));
 assert(!sdl_test_metadata_present[1]);assert(!sdl_test_metadata_present[2]);
 text(0x1000,"Tribes2.net");text(0x2000,"20250922");assert(call(5,0x1000,0x2000,0x4000));
 assert(!strcmp(sdl_test_metadata[0],"Tribes2.net")&&!strcmp(sdl_test_metadata[1],"20250922"));
 sdl_test_log_emit=1;call(7,15,0x87654321,0);
 assert(sdl_test_log_count==1&&sdl_test_log_userdata==0x87654321&&sdl_test_log_category==19&&sdl_test_log_priority==SDL_LOG_PRIORITY_ERROR);
 assert(!strcmp(sdl_test_log_message,"native caf\xc3\xa9"));assert(!strcmp(hint("SDL_LOG_REENTRY"),"works"));
 sdl_test_log_emit=2;call(7,16,0xfedcba98,0);assert(sdl_test_log_count==2&&sdl_test_log_userdata==0xfedcba98&&strlen(sdl_test_log_message)==2048);
 for(unsigned i=0;i<2048;i++)assert(sdl_test_log_message[i]=='L');
#ifdef CHECKED_TEST
 for(unsigned i=12;i<22;i++)fault(i);
#endif
 call(7,0,0x12345678,0);assert(sdl_test_log_count==2);
 sdl_test_log_emit=0;call(8,0,0,0);call(8,0,0,0);
 /* Real event lifecycle and native startup logging through the guest callback. */
 assert(call(10,0,0,0)==0);
 text(0x1000,SDL_HINT_LOGGING);text(0x2000,"verbose");assert(call(1,0x1000,0x2000,0));
 call(7,15,0x11223344,0);unsigned logs_before=sdl_test_log_count;
 assert(call(9,0,0,0));assert(call(10,0,0,0)==0);
 assert(sdl_test_log_count>logs_before&&sdl_test_log_userdata==0x11223344);
 assert(sdl_test_log_category==SDL_LOG_CATEGORY_SYSTEM);
 call(7,0,0,0);
 /* Native internal allocation is independent of guest public-buffer OOM. */
 assert(call(10,SDL_INIT_EVENTS,0,0)==0);
 call(7,15,0x11223344,0);assert(call(9,SDL_INIT_EVENTS,0,0)==1);
 assert(call(10,0,0,0)==SDL_INIT_EVENTS);assert(!call(10,SDL_INIT_VIDEO,0,0));
 assert(call(18,SDL_INIT_EVENTS,0,0)==1);call(17,SDL_INIT_EVENTS,0,0);assert(call(10,0,0,0)==SDL_INIT_EVENTS);
 call(17,SDL_INIT_EVENTS,0,0);assert(call(10,0,0,0)==0);call(17,SDL_INIT_EVENTS,0,0);
 assert(call(9,SDL_INIT_EVENTS,0,0)==1);assert(call(9,SDL_INIT_EVENTS,0,0)==1);
 unsigned live=sdl_test_live;assert(live&&sdl_test_allocations>128);
 call(19,0,0,0);assert(sdl_test_live<live);assert(call(10,0,0,0)==0);assert(call(9,SDL_INIT_EVENTS,0,0)==1);assert(call(10,0,0,0)==SDL_INIT_EVENTS);
 text(0x1000,"SDL_TIMER_RESOLUTION");text(0x2000,"1");assert(call(1,0x1000,0x2000,0));assert(!strcmp(hint("SDL_TIMER_RESOLUTION"),"1"));tribes_sdl_backend_quit(&cpu);
 printf("PASS: native SDL events (refcounts/query/subsystem quit/full quit/reinit), logging (cdecl copied UTF8/long messages, userdata, replacement/NULL, nested entry and LastError), metadata (copies/NULL/UTF8/invalid names), hints, 128 replacements, priorities/reset/NULL/public-copy OOM, private UTF8 environment, guest cdecl ABI and allocator; %u allocations, %u frees, %u live guest public buffers\n",sdl_test_allocations,sdl_test_frees,sdl_test_live);
}
