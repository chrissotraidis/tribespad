/* Tribes: Vengeance engine host (goal loop M1 onward, headless diagnostic).
 * Several original images (an executable and its DLLs) at their rebased guest
 * addresses (config/vengeance/layout.json) in the 4 GiB guest space, translated
 * ahead of time to ARM64 (scripts/vengeance/build_engine.py). Each DLL's
 * process attach runs in dependency order, then the executable's entry.
 * Guest pages are data, never executable. Unimplemented imports, helpers and
 * instructions stop with a JSON line on stdout; there are no success stubs.
 * Process setup follows Starsiege's host (port/starsiege/starsiege_engine.c).
 *
 * Usage: vengeance-engine IMAGES VOLUME GAME TEMP [arguments for the program]
 * GAME is mounted as C:\TribesPad\Game and holds the game's Program and
 * Content folders; the program runs from C:\TribesPad\Game\Program\Bin. */
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
#include "../core/guest_modules.h"
#include "../core/native_system_providers.h"
#include "../core/process_environment.h"
#include "../core/guest_profile.h"
#include "../core/process_startup.h"
#include "../core/guest_filesystem.h"
#include "../core/native_handles.h"
#include "../core/guest_display.h"
#include "../core/guest_graphics.h"
#include "../core/guest_dinput.h"
#include "vengeance_engine.inc"

extern uint32_t vg_enter(_cpu *cpu,uint32_t va,uint32_t count,const uint32_t *arguments);
extern void x87_fninit_void(_cpu *);
extern const uint32_t vengeance_com_count;
extern const char *const vengeance_com_names[];
extern void tb_sse_init(_cpu *);
static _cpu state;
static uint64_t base;
static pthread_mutex_t diagnostic_lock=PTHREAD_MUTEX_INITIALIZER;
enum {STACK_LOW=0x100000,STACK_HIGH=0x300000,PARAMETERS=0x3a0000,PEB=0x3b0000,TEB=0x3d0000,TLS_ARRAY=0x3e0000};

static int stack_readable(_cpu *c,uint32_t bytes){
 uint64_t address=base+(uint64_t)c->_esp;
 return c->_pointer_offset==base&&c->stack_bottom&&c->stack_top&&address>=(uintptr_t)c->stack_bottom&&address<=(uintptr_t)c->stack_top&&bytes<=(uintptr_t)c->stack_top-address;
}
static _cpu *diagnostic_cpu(void){_cpu *c=tribes_guest_callback_context();return c?c:&state;}
static _Noreturn void stop(_cpu *c,const char *kind,uint32_t index){
 assert(!pthread_mutex_lock(&diagnostic_lock));
 uint32_t ret=0;if(stack_readable(c,4))memcpy(&ret,(void *)(uintptr_t)(base+c->_esp),4);
 const char *name=!strcmp(kind,"import")&&index<sizeof import_names/sizeof import_names[0]?import_names[index]:
                  !strcmp(kind,"com")&&index<vengeance_com_count?vengeance_com_names[index]:"";
 printf("{\"diagnostic_stop\":true,\"kind\":\"%s\",\"index\":%u,\"name\":\"%s\",\"return_pc\":%u,\"esp\":%u,\"stack_arguments\":[",kind,index,name,ret,c->_esp);
 for(unsigned i=0;i<8;i++){uint32_t arg=0;if(stack_readable(c,4*(i+2)))memcpy(&arg,(void *)(uintptr_t)(base+c->_esp+4*(i+1)),4);printf("%s%u",i?",":"",arg);}
 printf("],\"registers\":{\"eax\":%u,\"ecx\":%u,\"edx\":%u,\"ebx\":%u,\"esp\":%u,\"ebp\":%u,\"esi\":%u,\"edi\":%u}}\n",c->_eax,c->_ecx,c->_edx,c->_ebx,c->_esp,c->_ebp,c->_esi,c->_edi);
 /* The first argument as text, when it is one (a file or library name, say). */
 uint32_t first=0;if(stack_readable(c,8))memcpy(&first,(void *)(uintptr_t)(base+c->_esp+4),4);
 /* Only read committed guest memory (TribesPad's VirtualQuery, into process scratch). */
 extern uint32_t tribes_virtual_query(uint64_t,uint32_t,uint32_t,uint32_t);
 uint32_t info[7]={0};int committed=0;
 if(first>=0x10000&&first<0xfffe0000u&&tribes_virtual_query(base,first,PARAMETERS+0x9000,28)){
  memcpy(info,(void *)(uintptr_t)(base+PARAMETERS+0x9000),28);committed=info[4]==0x1000&&(uint64_t)info[0]+info[3]>=(uint64_t)first+320;}
 if(committed){
  const unsigned char *p=(const unsigned char *)(uintptr_t)(base+first);char text[160];unsigned n=0;
  int wide=p[1]==0&&p[0]>=32&&p[0]<127;
  for(;n<sizeof text-1;n++){unsigned v=wide?p[2*n]|(p[2*n+1]<<8):p[n];if(!v)break;if(v<32||v>126){n=0;break;}text[n]=(char)v;}
  text[n]=0;if(n>=2)fprintf(stderr,"vengeance stop argument: %s\"%s\"\n",wide?"L":"",text);
 }
 fflush(stdout);_Exit(71);
}
_Noreturn void vengeance_missing_import(_cpu *c,uint32_t index){stop(c,"import",index);}
_Noreturn void vengeance_missing_com(_cpu *c,uint32_t index){stop(c,"com",index);}
/* Direct3D (port/vengeance/d3d9): the guest base, and the stop after a reported trap. */
uint64_t vengeance_guest_base(void){return base;}
uint32_t vengeance_control_proc(void){return VG_CONTROL_PROC;}
/* The guest address of a Windows import ("user32.dll!DefWindowProcW"), 0 if the game imports no such function. */
uint32_t vengeance_import_address(const char *name){
 for(unsigned i=0;i<sizeof import_names/sizeof import_names[0];i++)if(!strcmp(import_names[i],name))return 0xfffe0000u+16u*i;
 return 0;
}
_Noreturn void vengeance_d3d9_stop(_cpu *c){stop(c?c:diagnostic_cpu(),"d3d9",0);}
_Noreturn void halopad_trap_unimplemented(uint32_t pc){stop(diagnostic_cpu(),"instruction",pc);}
_Noreturn void tribes_unimplemented_dinput(void *c,uint32_t method){stop(c,"dinput",method);}
_Noreturn void tribes_missing_helper(uint32_t identifier){stop(diagnostic_cpu(),"helper",identifier);}
/* Helpers the runtime does not provide yet stay explicit stops. rdtsc and the
   repeated compares are real (vengeance_x86.c). Port I/O cannot run in a
   Windows program; reaching it means a wrong path. */
uint32_t X86_InterruptFlag=1;
void x86_in_al_dx(_cpu *c){stop(c,"port_io",0);}
void x86_in_ax_dx(_cpu *c){stop(c,"port_io",1);}
void x86_in_eax_dx(_cpu *c){stop(c,"port_io",2);}
void x86_out_dx_al(_cpu *c){stop(c,"port_io",3);}
void x86_out_dx_ax(_cpu *c){stop(c,"port_io",4);}
void x86_out_dx_eax(_cpu *c){stop(c,"port_io",5);}
/* Starsiege's GL wrappers report per-frame call counts; Vengeance does not trace them. */
void ss_gl_trace_report(uint64_t frame){(void)frame;}
void x86_int(_cpu *c,uint32_t number){stop(c,"interrupt",number);}

static void commit(uint32_t address,uint32_t bytes,uint32_t type){
 uint64_t page=(uint64_t)sysconf(_SC_PAGESIZE),lo=address&~(page-1),hi=((uint64_t)address+bytes+page-1)&~(page-1);
 assert(hi<=UINT64_C(0x100000000));assert(!mprotect((void *)(uintptr_t)(base+lo),(size_t)(hi-lo),PROT_READ|PROT_WRITE));
 assert(tribes_address_space_declare(base,(uint32_t)lo,(uint32_t)(hi-lo),(uint32_t)lo,4,0x1000,4,type));
}
static void load(const char *folder,const VgModule *m){
 char path[4096];snprintf(path,sizeof path,"%s/%s",folder,m->image);
 FILE *f=fopen(path,"rb");if(!f){fprintf(stderr,"vengeance: cannot open %s\n",path);exit(2);}
 assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size==(long)m->size&&!fseek(f,0,SEEK_SET));
 commit(m->base,m->size,0x1000000);assert(fread((void *)(uintptr_t)(base+m->base),1,(size_t)size,f)==(size_t)size&&!fclose(f));
}
static void put(uint32_t address,uint32_t value){memcpy((void *)(uintptr_t)(base+address),&value,4);}

/* FARPROC GetProcAddress(HMODULE, LPCSTR): a Vengeance module answers from its
   own PE export table (by name or ordinal); any other module goes to
   TribesPad's loader. Unreal finds native functions this way ("intUObjectexec..."). */
extern uint32_t tribes_get_proc_address(uint64_t base,uint32_t image,uint32_t pointer);
extern uint32_t tribes_set_last_error(uint32_t value);
uint32_t vg_get_proc_address(uint64_t guest,uint32_t image,uint32_t pointer){
 int ours=0;for(unsigned i=0;i<sizeof vg_modules/sizeof vg_modules[0];i++)if(vg_modules[i].base==image)ours=1;
 if(!ours)return tribes_get_proc_address(guest,image,pointer);
 if(pointer<=0xffff){
  for(unsigned i=0;i<sizeof vg_ordinals/sizeof vg_ordinals[0];i++)if(vg_ordinals[i].image==image&&vg_ordinals[i].ordinal==pointer)return vg_ordinals[i].address;
  tribes_set_last_error(127);return 0;
 }
 if(pointer>UINT32_MAX-1024){tribes_set_last_error(87);return 0;}
 const char *name=(const char *)(uintptr_t)(guest+pointer);if(strnlen(name,1024)==1024){tribes_set_last_error(127);return 0;}
 size_t lo=0,hi=sizeof vg_exports/sizeof vg_exports[0];
 while(lo<hi){size_t mid=lo+(hi-lo)/2;const VgExport *e=&vg_exports[mid];
  int order=e->image<image?-1:e->image>image?1:strcmp(e->name,name);
  if(!order)return e->address;if(order<0)lo=mid+1;else hi=mid;}
 tribes_set_last_error(127);return 0;
}

int main(int argc,char **argv){
 if(argc<5){fprintf(stderr,"usage: %s images volume game temp [arguments]\n",argv[0]);return 2;}
 const unsigned count=sizeof vg_modules/sizeof vg_modules[0];
 /* Desktop SDL runs on process main; the backend checks this before video. */
 extern void tribes_sdl_host_application_ready(void);
 tribes_sdl_host_application_ready();
 void *memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 base=(uintptr_t)memory;assert(tribes_absolute_memory_bind(base));
 assert(tribes_address_space_begin(base));
 for(unsigned i=0;i<count;i++)load(argv[1],&vg_modules[i]);
 commit(STACK_LOW,STACK_HIGH-STACK_LOW,0x20000);commit(PEB,0x1000,0x20000);commit(PARAMETERS,0x10000,0x20000);
 commit(TEB,0x1000,0x20000);commit(TLS_ARRAY,0x1000,0x20000);assert(tribes_address_space_seal(base));
 for(unsigned i=0;i<sizeof vg_imports/sizeof vg_imports[0];++i)put(vg_imports[i][0],vg_imports[i][1]);
 uint64_t tid;assert(!pthread_threadid_np(NULL,&tid));assert((uint32_t)tid);
 assert(tribes_teb_configure(base,TEB,STACK_HIGH,STACK_LOW,(uint32_t)getpid(),(uint32_t)tid,TLS_ARRAY));
 const VgModule *exe=NULL;TribesThreadModule thread_modules[sizeof vg_modules/sizeof vg_modules[0]];
 for(unsigned i=0;i<count;i++){
  const VgModule *m=&vg_modules[i];if(!m->dll)exe=m;
  assert(tribes_module_register_image(base,m->base,m->size,m->path,!m->dll));
  /* No static TLS in any module (scripts/vengeance/layout.py); DLL entries get
     thread notifications. A DLL the program loads by name (renderer, audio,
     network...) stays pending until then and attaches on LoadLibrary. */
  thread_modules[i]=(TribesThreadModule){m->base,m->dll?m->entry:0,UINT32_MAX,0,0,0,NULL,NULL,m->dynamic};
  if(m->dynamic)assert(tribes_module_set_pending(base,m->base,1));
 }
 assert(exe);
 assert(tribes_threads_configure(base,(uint32_t)getpid(),(uint32_t)tid,VG_STACK_RESERVE,thread_modules,count));
 assert(tribes_system_providers_configure(base));
 /* The guest desktop: VENGEANCE_DISPLAY (WIDTHxHEIGHT), or 1024x768 when headless. */
 uint32_t display_width=1024,display_height=768;const char *display=getenv("VENGEANCE_DISPLAY");
 if(display&&sscanf(display,"%ux%u",&display_width,&display_height)!=2){fprintf(stderr,"VENGEANCE_DISPLAY must be WIDTHxHEIGHT\n");return 2;}
 assert(tribes_display_configure(base,display_width,display_height));
 assert(tribes_graphics_configure(base));
 /* SDL keeps its own allocations in guest memory (port/starsiege/starsiege_heap.c). */
 extern uint32_t tribes_sdl_set_memory_functions(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t);
 assert(tribes_sdl_set_memory_functions(base,sdl_allocator[0],sdl_allocator[1],sdl_allocator[2],sdl_allocator[3]));
 /* DirectInput keyboard0 and mouse0 (port/starsiege/starsiege_input.c feeds them;
    WinDrv reaches them through DirectInput 8, port/vengeance/vengeance_dinput.c). */
 assert(tribes_dinput_configure(base));assert(tribes_dinput_set_devices(base,vg_input_devices,sizeof vg_input_devices/sizeof vg_input_devices[0]));
 assert(tribes_environment_configure(base,environment,sizeof environment));
 /* Command line: the program's full path, quoted, then the given arguments. */
 static char line[8192];int used=snprintf(line,sizeof line,"\"%s%s\"",VG_GUEST_BIN,VG_EXE_NAME);
 for(int i=5;i<argc&&used>0&&(size_t)used<sizeof line;i++)used+=snprintf(line+used,sizeof line-(size_t)used,strchr(argv[i],' ')?" \"%s\"":" %s",argv[i]);
 assert(used>0&&(size_t)used<sizeof line);
 static uint16_t wide[8192];for(int i=0;i<=used;i++)wide[i]=(unsigned char)line[i];
 assert(tribes_command_line_configure(base,line,(uint32_t)used+1));
 assert(tribes_command_line_configure_w(base,wide,2u*((uint32_t)used+1)));
 assert(tribes_filesystem_configure(base,argv[2],argv[3],argv[4]));assert(tribes_named_mutex_configure(argv[4]));
 /* Unreal programs run from their Program\Bin folder (startup.ini, the INIs
    and ..\Content paths are relative to it), as the game's shortcuts start them. */
 {static const char bin[]=VG_GUEST_BIN;char folder[sizeof bin];memcpy(folder,bin,sizeof bin);folder[sizeof bin-2]=0;
  memcpy((void *)(uintptr_t)(base+PARAMETERS+0x8000),folder,sizeof folder);
  if(!tribes_set_current_directory(base,PARAMETERS+0x8000)){fprintf(stderr,"vengeance: no %s folder in the game\n",folder);return 2;}}
 assert(tribes_temp_directory_configure(base,VG_WINDOWS_DIRECTORY));assert(tribes_profile_configure_file(base,VG_PROFILE_FILE));
 uint32_t handles[3];for(unsigned i=0;i<3;++i)handles[i]=tribes_get_std_handle(0xfffffff6-i);assert(tribes_startup_configure(base,1,handles));
 uint32_t image_name_chars=tribes_get_module_filename_w(base,0,PARAMETERS+0x1000,1024);assert(image_name_chars&&image_name_chars<1024);
 uint32_t wide_environment=tribes_get_environment_strings_w(base),environment_bytes=tribes_environment_snapshot_size(base,wide_environment,1);
 assert(wide_environment&&environment_bytes<=0xe000);
 memcpy((void *)(uintptr_t)(base+PARAMETERS+0x2000),(void *)(uintptr_t)(base+wide_environment),environment_bytes);assert(tribes_free_environment_strings_w(base,wide_environment));
 TribesProcessMetadata process_metadata={PEB,PARAMETERS,exe->base,PARAMETERS+0x1000,image_name_chars*2,tribes_get_command_line_w(base),2u*(uint32_t)used,PARAMETERS+0x2000,{handles[0],handles[1],handles[2]}};
 assert(tribes_teb_configure_process(base,&process_metadata));
 state._pointer_offset=base;state.stack_bottom=(void *)(uintptr_t)(base+STACK_LOW);state.stack_top=(void *)(uintptr_t)(base+STACK_HIGH);
 x87_fninit_void(&state);tb_sse_init(&state);
 state._esp=STACK_HIGH-0x100;state._eflags=0x202;
 /* Process attach, in dependency order (DllMain(instance, DLL_PROCESS_ATTACH, NULL)). */
 for(unsigned i=0;i<count;i++){
  const VgModule *m=&vg_modules[i];if(!m->dll||m->dynamic)continue;
  uint32_t args[]={m->base,1,0};
  state._esp=STACK_HIGH-0x100;
  uint32_t result=vg_enter(&state,m->entry,3,args);
  fprintf(stderr,"vengeance: %s attach result %u\n",m->path,result);
  if(!result){printf("{\"diagnostic_stop\":true,\"kind\":\"dll_attach_failed\",\"name\":\"%s\"}\n",m->path);fflush(stdout);return 72;}
 }
 extern void tribes_threads_process_ready(uint64_t);
 tribes_threads_process_ready(base);
 fprintf(stderr,"vengeance: entering %s at 0x%08x (%s)\n",VG_EXE_NAME,exe->entry,line);
 state._esp=STACK_HIGH-0x100;
 vg_enter(&state,exe->entry,0,NULL);
 printf("{\"diagnostic_stop\":false,\"returned\":true,\"eax\":%u}\n",state._eax);
 return 0;
}
