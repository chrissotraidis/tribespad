#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t),tribes_return_to_host(void);
extern void tbimp_SDL_IOFromFile(void),tbimp_SDL_CloseIO(void),tbimp_SDL_SeekIO(void),tbimp_SDL_TellIO(void),tbimp_SDL_GetIOSize(void),tbimp_SDL_ReadIO(void),tbimp_SDL_ReadU32LE(void),tbimp_SDL_GetSIMDAlignment(void);
static unsigned char *memory;
static uint64_t call(unsigned entry,uint32_t a,uint32_t b,uint32_t c,uint32_t d){
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x100800;
 uint32_t args[]={a,b,c,d};memcpy(memory+cpu._esp,args,sizeof args);_cpu before=cpu;
 tribes_set_last_error(123);tribes_enter(&cpu,entry);uint64_t result=cpu._eax;
 if(entry==3||entry==4||entry==5){result|=(uint64_t)cpu._edx<<32;before._edx=cpu._edx;}
 before._eax=cpu._eax;assert(!memcmp(&before,&cpu,sizeof cpu));assert(tribes_get_last_error()==123);return result;
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(!mprotect(memory+0x100000,0x10000,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));assert(tribes_memory_permissions_set(0,UINT64_C(0x100000000),0));assert(tribes_memory_permissions_set(0x100000,0x10000,3));
 const void *targets[]={tbimp_SDL_IOFromFile,tbimp_SDL_CloseIO,tbimp_SDL_SeekIO,tbimp_SDL_TellIO,tbimp_SDL_GetIOSize,tbimp_SDL_ReadIO,tbimp_SDL_ReadU32LE,tbimp_SDL_GetSIMDAlignment,tribes_return_to_host};tribes_memory_register_targets(targets,9);tribes_memory_external_configure((uintptr_t)memory,UINT64_C(0x100000000));tribes_memory_external_enable();
 char dir[]="/tmp/tribes-sdl-io-XXXXXX";assert(mkdtemp(dir));assert(tribes_filesystem_configure((uintptr_t)memory,dir,dir,dir));char path[1024];snprintf(path,sizeof path,"%s/Music.dat",dir);FILE *f=fopen(path,"wb");assert(f);assert(fwrite("abc\0def",1,7,f)==7);fclose(f);
 strcpy((char *)memory+0x101000,"music.DAT");strcpy((char *)memory+0x102000,"rb");
 if(argc>1&&!strcmp(argv[1],"unreadable-path")){assert(tribes_memory_permissions_set(0x101000,4096,0));call(1,0x101000,0x102000,0,0);abort();}
 uint32_t token=(uint32_t)call(1,0x101000,0x102000,0,0);assert(token);unlink(path);rmdir(dir); /* Open stream must retain actual file. */
 if(argc>1){
  if(!strcmp(argv[1],"stale")){assert(call(2,token,0,0,0));call(4,token,0,0,0);}
  else if(!strcmp(argv[1],"foreign"))call(4,token+1,0,0,0);
  else if(!strcmp(argv[1],"readonly-output")){assert(tribes_memory_permissions_set(0x103000,4096,1));call(6,token,0x103000,7,0);}
  else if(!strcmp(argv[1],"short-output"))call(6,token,0x10fffd,7,0);
  else abort();abort();
 }
 assert(call(8,0,0,0,0)==SDL_GetSIMDAlignment());
 assert(call(7,token,0x103001,0,0)==1);uint32_t word;memcpy(&word,memory+0x103001,4);assert(word==0x00636261);
 assert(call(7,token,0x103001,0,0)==0);memcpy(&word,memory+0x103001,4);assert(word==0x00666564);
 assert(call(7,token,0x103001,0,0)==0);memcpy(&word,memory+0x103001,4);assert(word==0);
 assert(call(3,token,0,0,SDL_IO_SEEK_SET)==0);assert(call(7,token,0,0,0)==1);assert(call(4,token,0,0,0)==4);assert(call(3,token,0,0,SDL_IO_SEEK_SET)==0);
 assert(call(5,token,0,0,0)==7&&call(4,token,0,0,0)==0);
 memset(memory+0x103000,0x7e,32);assert(call(6,token,0x103001,12,0)==7);assert(!memcmp(memory+0x103001,"abc\0def",7));assert(memory[0x103000]==0x7e&&memory[0x103008]==0x7e);assert(call(4,token,0,0,0)==7);
 /* EOF has no destination access; zero-length read also leaves it alone. */
 assert(call(6,token,0xffffffff,12,0)==0);assert(call(6,token,0xffffffff,0,0)==0);
 assert(call(3,token,(uint32_t)-3,UINT32_MAX,SDL_IO_SEEK_END)==4);assert(call(6,token,0x103001,3,0)==3&&!memcmp(memory+0x103001,"def",3));
 assert(call(3,token,5,1,SDL_IO_SEEK_SET)==UINT64_C(0x100000005));assert(call(4,token,0,0,0)==UINT64_C(0x100000005));assert(call(5,token,0,0,0)==7);assert(call(4,token,0,0,0)==UINT64_C(0x100000005));
 assert(call(3,token,UINT32_MAX,UINT32_MAX,SDL_IO_SEEK_SET)==UINT64_MAX);assert(SDL_GetError()[0]);
 assert(call(2,token,0,0,0));assert(call(1,0x101000,0x102000,0,0)==0);assert(SDL_GetError()[0]);assert(call(4,0,0,0,0)==UINT64_MAX);
 puts("PASS: real SDL file bytes, EOF, signed/64-bit seek and size, cdecl CPU/LastError, checked output extent and owned stream lifetime");return 0;
}
