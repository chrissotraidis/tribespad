/* Starsiege: Tribes 1.41 engine entry (goal loop G2, headless diagnostic).
 * One original image (Tribes.exe) at its PE address in the 4 GiB guest space,
 * translated ahead of time to ARM64 (scripts/starsiege/build_engine.py).
 * Guest pages are data, never executable. Unimplemented imports, helpers and
 * instructions stop with a JSON line on stdout; there are no success stubs.
 * Memory layout follows Tribes 2's bootstrap (tests/qol_native_bootstrap.c). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include "../core/guest_threads.h"
#include "../llasm-support/tribes_absolute_memory.h"
#include "../core/guest_teb.h"
#include "../core/guest_address_space.h"
#include "../core/guest_virtual_memory.h"
#include "../core/guest_heap.h"
#include "../core/guest_modules.h"
#include "../core/native_system_providers.h"
#include "../core/process_environment.h"
#include "../core/guest_profile.h"
#include "../core/process_startup.h"
#include "../core/guest_filesystem.h"
#include "../core/native_handles.h"
#include "../core/guest_display.h"
#include "../core/guest_graphics.h"
#include "starsiege_engine.inc"

extern void tribes_enter(_cpu *,uint32_t);
extern void x87_fninit_void(_cpu *);
extern void tb_sse_init(_cpu *);
static _cpu state;
static uint64_t base;
static pthread_mutex_t diagnostic_lock=PTHREAD_MUTEX_INITIALIZER;

static int stack_readable(_cpu *c,uint32_t bytes){
 uint64_t address=base+(uint64_t)c->_esp;
 return c->_pointer_offset==base&&c->stack_bottom&&c->stack_top&&address>=(uintptr_t)c->stack_bottom&&address<=(uintptr_t)c->stack_top&&bytes<=(uintptr_t)c->stack_top-address;
}
static _cpu *diagnostic_cpu(void){_cpu *c=tribes_guest_callback_context();return c?c:&state;}
static _Noreturn void stop(_cpu *c,const char *kind,uint32_t index){
 assert(!pthread_mutex_lock(&diagnostic_lock));
 uint32_t ret=0;if(stack_readable(c,4))memcpy(&ret,(void *)(uintptr_t)(base+c->_esp),4);
 const char *name=!strcmp(kind,"import")&&index<sizeof import_names/sizeof import_names[0]?import_names[index]:"";
 printf("{\"diagnostic_stop\":true,\"kind\":\"%s\",\"index\":%u,\"name\":\"%s\",\"return_pc\":%u,\"esp\":%u,\"stack_arguments\":[",kind,index,name,ret,c->_esp);
 for(unsigned i=0;i<8;i++){uint32_t arg=0;if(stack_readable(c,4*(i+2)))memcpy(&arg,(void *)(uintptr_t)(base+c->_esp+4*(i+1)),4);printf("%s%u",i?",":"",arg);}
 printf("],\"registers\":{\"eax\":%u,\"ecx\":%u,\"edx\":%u,\"ebx\":%u,\"esp\":%u,\"ebp\":%u,\"esi\":%u,\"edi\":%u}}\n",c->_eax,c->_ecx,c->_edx,c->_ebx,c->_esp,c->_ebp,c->_esi,c->_edi);
 fflush(stdout);_Exit(71);
}
_Noreturn void starsiege_missing_import(_cpu *c,uint32_t index){stop(c,"import",index);}
_Noreturn void halopad_trap_unimplemented(uint32_t pc){stop(diagnostic_cpu(),"instruction",pc);}
_Noreturn void tribes_unimplemented_dinput(void *c,uint32_t method){stop(c,"dinput",method);}
_Noreturn void tribes_missing_helper(uint32_t identifier){stop(diagnostic_cpu(),"helper",identifier);}
/* Helpers the runtime does not provide yet stay explicit stops. */
void x86_rdtsc(_cpu *c){stop(c,"rdtsc",0);}
void x86_repe_cmpsw_prep(_cpu *c){stop(c,"repe_cmpsw",0);}
uint32_t X86_InterruptFlag=1;
void x86_int(_cpu *c,uint32_t number){stop(c,"interrupt",number);}
void x87_fnsave_void(_cpu *c,uint8_t *memory){(void)memory;stop(c,"fnsave",0);}
void x87_frstor_void(_cpu *c,const uint8_t *memory){(void)memory;stop(c,"frstor",0);}

static void commit(uint32_t address,uint32_t bytes,uint32_t type){
 uint64_t page=(uint64_t)sysconf(_SC_PAGESIZE),lo=address&~(page-1),hi=((uint64_t)address+bytes+page-1)&~(page-1);
 assert(hi<=UINT64_C(0x100000000));assert(!mprotect((void *)(uintptr_t)(base+lo),(size_t)(hi-lo),PROT_READ|PROT_WRITE));
 assert(tribes_address_space_declare(base,(uint32_t)lo,(uint32_t)(hi-lo),(uint32_t)lo,4,0x1000,4,type));
}
static void load(const char *path,uint32_t address){
 FILE *f=fopen(path,"rb");assert(f&&!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0&&size<0x10000000&&!fseek(f,0,SEEK_SET));
 commit(address,(uint32_t)size,0x1000000);assert(fread((void *)(uintptr_t)(base+address),1,(size_t)size,f)==(size_t)size&&!fclose(f));
}
static void put(uint32_t address,uint32_t value){memcpy((void *)(uintptr_t)(base+address),&value,4);}

int main(int argc,char **argv){
 /* image.bin, volume root, game folder (C:\TribesPad\Game), temp folder */
 if(argc!=5){fprintf(stderr,"usage: %s image.bin volume game temp\n",argv[0]);return 2;}
 /* Desktop SDL runs on process main; the backend checks this before video. */
 extern void tribes_sdl_host_application_ready(void);
 tribes_sdl_host_application_ready();
 void *memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 base=(uintptr_t)memory;assert(tribes_absolute_memory_bind(base));
 assert(tribes_address_space_begin(base));load(argv[1],SS_IMAGE_BASE);
 commit(0x100000,0x200000,0x20000);commit(0x3b0000,0x1000,0x20000);commit(0x3a0000,0x10000,0x20000);
 commit(0x3d0000,0x1000,0x20000);commit(0x3e0000,0x1000,0x20000);assert(tribes_address_space_seal(base));
 for(unsigned i=0;i<sizeof imports/sizeof imports[0];++i)put(imports[i][0],imports[i][1]);
 uint64_t tid;assert(!pthread_threadid_np(NULL,&tid));assert((uint32_t)tid);
 assert(tribes_teb_configure(base,0x3d0000,0x300000,0x100000,(uint32_t)getpid(),(uint32_t)tid,0x3e0000));
 assert(tribes_module_register_image(base,SS_IMAGE_BASE,SS_IMAGE_SIZE,"C:\\TribesPad\\Game\\Tribes.exe",1));
 /* The executable has no static TLS and no attach entry. */
 const TribesThreadModule thread_modules[]={{SS_IMAGE_BASE,0,UINT32_MAX,0,0,0,NULL,NULL,0}};
 assert(tribes_threads_configure(base,(uint32_t)getpid(),(uint32_t)tid,SS_STACK_RESERVE,thread_modules,1));
 assert(tribes_system_providers_configure(base));
 /* The guest desktop: the host's screen, or 1024x768 when headless. */
 uint32_t display_width=1024,display_height=768;const char *display=getenv("STARSIEGE_DISPLAY");
 if(display&&sscanf(display,"%ux%u",&display_width,&display_height)!=2){fprintf(stderr,"STARSIEGE_DISPLAY must be WIDTHxHEIGHT\n");return 2;}
 assert(tribes_display_configure(base,display_width,display_height));
 assert(tribes_graphics_configure(base));
 /* SDL keeps its own allocations in guest memory (see starsiege_heap.c). */
 extern uint32_t tribes_sdl_set_memory_functions(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t);
 assert(tribes_sdl_set_memory_functions(base,sdl_allocator[0],sdl_allocator[1],sdl_allocator[2],sdl_allocator[3]));
 /* Community plugins and Windows-only libraries: a real "not present". */
 for(unsigned i=0;i<sizeof absent_modules/sizeof absent_modules[0];i++)assert(tribes_module_define_absent(base,absent_modules[i]));
 assert(tribes_environment_configure(base,environment,sizeof environment));assert(tribes_command_line_configure(base,command_line,sizeof command_line));
 assert(tribes_filesystem_configure(base,argv[2],argv[3],argv[4]));assert(tribes_named_mutex_configure(argv[4]));
 assert(tribes_temp_directory_configure(base,SS_WINDOWS_DIRECTORY));assert(tribes_profile_configure_file(base,SS_PROFILE_FILE));
 assert(tribes_command_line_configure_w(base,wide_command_line,sizeof wide_command_line));
 uint32_t handles[3];for(unsigned i=0;i<3;++i)handles[i]=tribes_get_std_handle(0xfffffff6-i);assert(tribes_startup_configure(base,1,handles));
 uint32_t image_name_chars=tribes_get_module_filename_w(base,0,0x3a1000,1024);assert(image_name_chars&&image_name_chars<1024);
 uint32_t wide_environment=tribes_get_environment_strings_w(base),environment_bytes=tribes_environment_snapshot_size(base,wide_environment,1);
 assert(wide_environment&&environment_bytes<=0xe000);
 memcpy((void *)(uintptr_t)(base+0x3a2000),(void *)(uintptr_t)(base+wide_environment),environment_bytes);assert(tribes_free_environment_strings_w(base,wide_environment));
 TribesProcessMetadata process_metadata={0x3b0000,0x3a0000,SS_IMAGE_BASE,0x3a1000,image_name_chars*2,tribes_get_command_line_w(base),sizeof wide_command_line-2,0x3a2000,{handles[0],handles[1],handles[2]}};
 assert(tribes_teb_configure_process(base,&process_metadata));
 state._pointer_offset=base;state.stack_bottom=(void *)(uintptr_t)(base+0x100000);state.stack_top=(void *)(uintptr_t)(base+0x300000);
 x87_fninit_void(&state);tb_sse_init(&state);
 state._esp=0x2fff00;state._eflags=0x202;
 fprintf(stderr,"Entering Starsiege: Tribes at 0x%08x\n",SS_ENTRY);
 tribes_enter(&state,SS_ENTRY);
 printf("{\"diagnostic_stop\":false,\"returned\":true,\"eax\":%u}\n",state._eax);
 return 0;
}


/* GL call counts per frame (STARSIEGE_TRACE_GL=1), reported by the swap. */
static uint64_t gl_counts[sizeof gl_trace_names/sizeof gl_trace_names[0]];
static int gl_tracing=-1;
void ss_gl_trace(uint32_t index){
 if(gl_tracing<0){const char *v=getenv("STARSIEGE_TRACE_GL");gl_tracing=v&&!strcmp(v,"1");}
 if(gl_tracing&&index<sizeof gl_counts/sizeof gl_counts[0])gl_counts[index]++;
}
void ss_gl_trace_report(uint64_t frame){
 if(gl_tracing!=1)return;
 fprintf(stderr,"TribesPad: GL calls before frame %llu:",(unsigned long long)frame);
 for(unsigned i=0;i<sizeof gl_counts/sizeof gl_counts[0];i++)if(gl_counts[i])fprintf(stderr," %s=%llu",gl_trace_names[i],(unsigned long long)gl_counts[i]);
 fputc('\n',stderr);memset(gl_counts,0,sizeof gl_counts);
}
