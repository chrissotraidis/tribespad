#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_modules.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tbimp_LoadLibraryA(void),tbimp_LoadLibraryExW(void),tbimp_GetModuleHandleA(void),tbimp_GetModuleHandleW(void),tbimp_FreeLibrary(void),tbimp_GetProcAddress(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){switch(pc){case 1:return (void *)tbimp_LoadLibraryA;case 2:return (void *)tbimp_LoadLibraryExW;case 3:return (void *)tbimp_GetModuleHandleA;case 4:return (void *)tbimp_GetModuleHandleW;case 5:return (void *)tbimp_FreeLibrary;case 6:return (void *)tbimp_GetProcAddress;case 0xfffff000:return (void *)tribes_return_to_host;}abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static unsigned char *memory,*other;static uint32_t cursor=0x200001,absent[6],legacy_audio[3],available_a,available_w,unknown,path,export_name;
static uint32_t save(const char *s,int wide){uint32_t at=cursor;for(unsigned i=0;i<=strlen(s);i++){uint16_t unit=(unsigned char)s[i];memcpy(memory+cursor,&unit,wide?2:1);cursor+=wide?2:1;}cursor+=3;assert(cursor<0x210000);return at;}
static void setup(_cpu *cpu,unsigned id,unsigned char *window){memset(cpu,0xa5,sizeof *cpu);cpu->_pointer_offset=(uintptr_t)window;cpu->_esp=0x10f003+id*65536;}
static uint32_t invoke(_cpu *cpu,unsigned id,const uint32_t *args){static const unsigned counts[]={0,1,3,1,1,1,2};unsigned n=counts[id];uint32_t top=cpu->_esp,sp=top-4*n;unsigned char *m=(void *)(uintptr_t)cpu->_pointer_offset;memcpy(m+sp,args,4*n);memset(m+sp-68,0x79,64);memset(m+top,0x79,64);cpu->_esp=sp;_cpu before=*cpu;tribes_enter(cpu,id);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(cpu,&before,sizeof before)&&!memcmp(m+sp,args,4*n));for(unsigned i=0;i<64;i++)assert(m[sp-68+i]==0x79&&m[top+i]==0x79);return result;}
static void expect(_cpu *cpu,unsigned id,const uint32_t *args,uint32_t result,uint32_t error){tribes_set_last_error(123);assert(invoke(cpu,id,args)==result&&tribes_get_last_error()==error);}
static void *worker(void *arg){unsigned id=(unsigned)(uintptr_t)arg;_cpu cpu;setup(&cpu,id+1,memory);for(unsigned i=0;i<1000;i++){
 uint32_t a[]={absent[i%3]},w[]={absent[3+i%3],0,0};expect(&cpu,1,a,0,126);expect(&cpu,2,w,0,126);expect(&cpu,3,a,0,126);expect(&cpu,4,w,0,126);
 uint32_t load[]={available_a},close[]={0x60010000},lookup[]={0x60010000,export_name};expect(&cpu,1,load,0x60010000,123);expect(&cpu,6,lookup,0x60020000,123);expect(&cpu,5,close,1,123);
 }return NULL;}
static unsigned char *window(void){unsigned char *p=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(p!=MAP_FAILED&&!mprotect(p+0x100000,0x90000,PROT_READ|PROT_WRITE)&&!mprotect(p+0x200000,0x10000,PROT_READ|PROT_WRITE));return p;}
int main(int argc,char **argv){memory=window();other=window();_cpu cpu,isolated;setup(&cpu,0,memory);setup(&isolated,0,other);const char *names[]={"dbghelp.dll","DBGHELP.DLL","dbghelp"};for(unsigned width=0;width<2;width++)for(unsigned i=0;i<3;i++)absent[width*3+i]=save(names[i],width);legacy_audio[0]=save("OpenAL.dll",0);legacy_audio[1]=save("OPENAL",0);legacy_audio[2]=save("openal.dll",1);available_a=save("kernel32.dll",0);available_w=save("KERNEL32",1);unknown=save("present-but-unported.dll",0);path=save("C:\\Other\\dbghelp.dll",0);export_name=save("Example",0);memcpy(other+0x200000,memory+0x200000,0x10000);unsigned char before[65536];memcpy(before,memory+0x200000,sizeof before);assert(!mprotect(memory+0x200000,0x10000,PROT_READ)&&!mprotect(other+0x200000,0x10000,PROT_READ));
 assert(tribes_module_define_absent((uintptr_t)memory,"dbghelp"));if(argc>1){uint32_t a[]={!strcmp(argv[1],"unknown")?unknown:!strcmp(argv[1],"explicit-path")?path:absent[0]};if(!strcmp(argv[1],"foreign-process"))expect(&isolated,1,a,0,126);else expect(&cpu,1,a,0,126);abort();}
 assert(tribes_module_define_absent((uintptr_t)memory,"OpenAL.dll"));
 for(unsigned i=0;i<2;i++){uint32_t a[]={legacy_audio[i]};expect(&cpu,1,a,0,126);expect(&cpu,3,a,0,126);}
 uint32_t audio_w[]={legacy_audio[2],0,0};expect(&cpu,2,audio_w,0,126);expect(&cpu,4,audio_w,0,126);
 assert(!tribes_module_define_absent((uintptr_t)memory,"DBGHELP.dll")&&errno==EALREADY);const char *invalid[]={"","C:\\bad.dll","sub/name.dll","bad?name.dll","bad.","caf\xe9.dll"};for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++)assert(!tribes_module_define_absent((uintptr_t)memory,invalid[i])&&errno==EINVAL);assert(!tribes_module_define_absent((uintptr_t)memory,NULL)&&errno==EINVAL);
 TribesNativeExport exports[]={{"Example",1,0x60020000}};assert(!tribes_module_register((uintptr_t)memory,0x500000,"C:\\TribesPad\\Game\\DBGHELP.dll",0)&&errno==EALREADY);assert(!tribes_module_define_native((uintptr_t)memory,0x60010000,"C:\\Windows\\System32\\dbghelp.dll",exports,1)&&errno==EALREADY);
 assert(tribes_module_define_native((uintptr_t)memory,0x60010000,"C:\\Windows\\System32\\kernel32.dll",exports,1));assert(!tribes_module_define_absent((uintptr_t)memory,"kernel32.dll")&&errno==EALREADY);
 assert(tribes_module_define_native((uintptr_t)other,0x60030000,"C:\\Windows\\System32\\dbghelp.dll",exports,1));uint32_t other_load[]={absent[0]},other_close[]={0x60030000};expect(&isolated,1,other_load,0x60030000,123);expect(&isolated,5,other_close,1,123);
 for(unsigned i=0;i<3;i++){uint32_t a[]={absent[i]},w[]={absent[3+i],0,0};expect(&cpu,1,a,0,126);expect(&cpu,2,w,0,126);expect(&cpu,3,a,0,126);expect(&cpu,4,w,0,126);w[2]=0x800;expect(&cpu,2,w,0,87);}
 uint32_t missing_proc[]={0,export_name},missing_free[]={0},null[]={0,0,0};expect(&cpu,6,missing_proc,0,6);expect(&cpu,5,missing_free,0,6);expect(&cpu,1,null,0,87);expect(&cpu,2,null,0,87);
 uint32_t wide_load[]={available_w,0,0},lookup[]={0x60010000,export_name};expect(&cpu,2,wide_load,0x60010000,123);expect(&cpu,6,lookup,0x60020000,123);
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(threads+i,NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));expect(&cpu,6,lookup,0x60020000,123);uint32_t close[]={0x60010000};expect(&cpu,5,close,1,123);expect(&cpu,5,close,0,6);uint32_t provider_handle[]={available_a};expect(&cpu,3,provider_handle,0,126);assert(!memcmp(before,memory+0x200000,sizeof before)&&!memcmp(before,other+0x200000,sizeof before));
 puts("PASS: actual ARM64 LoadLibraryA/LoadLibraryExW/GetModuleHandleA/W/FreeLibrary/GetProcAddress wrappers; declared bare-name absence returns NULL/error 126 without module/export publication; case/extension normalization, conflicts and process isolation, readonly unaligned names, full CPU/FP/flags/stack and LastError; 56000 concurrent wrapper calls with retained-provider load/export/free references unchanged");return 0;
}
