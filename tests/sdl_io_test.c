#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_sdl_memory.h"
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
extern void tbimp_SDL_strcasecmp(void),tbimp_SDL_strlcpy(void);
extern void tbimp_SDL_IOFromFile(void),tbimp_SDL_CloseIO(void),tbimp_SDL_SeekIO(void),tbimp_SDL_TellIO(void),tbimp_SDL_GetIOSize(void),tbimp_SDL_ReadIO(void),tbimp_SDL_ReadU32LE(void),tbimp_SDL_GetSIMDAlignment(void);
extern void tbimp_SDL_IOFromConstMem(void),tbimp_SDL_LoadWAV_IO(void),tbimp_test_malloc(void),tbimp_SDL_ClearError(void);
static unsigned char *memory;
static uint64_t call5(unsigned entry,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e){
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x100800;
 cpu.stack_bottom=memory+0x100000;cpu.stack_top=memory+0x110000;
 uint32_t args[]={a,b,c,d,e};memcpy(memory+cpu._esp,args,sizeof args);_cpu before=cpu;
 tribes_set_last_error(123);tribes_enter(&cpu,entry);uint64_t result=cpu._eax;
 if(entry==3||entry==4||entry==5){result|=(uint64_t)cpu._edx<<32;before._edx=cpu._edx;}
 before._eax=cpu._eax;assert(!memcmp(&before,&cpu,sizeof cpu));assert(tribes_get_last_error()==123);return result;
}
static uint64_t call(unsigned entry,uint32_t a,uint32_t b,uint32_t c,uint32_t d){return call5(entry,a,b,c,d,0);}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(!mprotect(memory+0x100000,0x10000,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));assert(tribes_memory_permissions_set(0,UINT64_C(0x100000000),0));assert(tribes_memory_permissions_set(0x100000,0x10000,3));
 const void *targets[]={tbimp_SDL_IOFromFile,tbimp_SDL_CloseIO,tbimp_SDL_SeekIO,tbimp_SDL_TellIO,tbimp_SDL_GetIOSize,tbimp_SDL_ReadIO,tbimp_SDL_ReadU32LE,tbimp_SDL_GetSIMDAlignment,tbimp_SDL_strcasecmp,tbimp_SDL_strlcpy,tbimp_SDL_IOFromConstMem,tbimp_SDL_LoadWAV_IO,tbimp_test_malloc,tbimp_SDL_ClearError,tribes_return_to_host};tribes_memory_register_targets(targets,15);tribes_memory_external_configure((uintptr_t)memory,UINT64_C(0x100000000));tribes_memory_external_enable();
 assert(tribes_memory_permissions_set(0x30000000,0x1000000,3)); /* Standalone guest heap arena. */
 assert(tribes_sdl_set_memory_functions((uintptr_t)memory,14,14,14,14));
 /* Exact PCM bytes, scalar token lifetime, and failure output semantics. */
 unsigned char wav[]={ 'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
  1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0x34,0x12,0xdc,0xfe};
 memcpy(memory+0x104000,wav,sizeof wav);
 if(argc>1&&!strcmp(argv[1],"memory-unreadable")){assert(tribes_memory_permissions_set(0x104000,4096,0));call(11,0x104000,sizeof wav,0,0);abort();}
 uint32_t wt=(uint32_t)call(11,0x104000,sizeof wav,0,0);assert(wt);
 if(argc>1&&!strcmp(argv[1],"wav-readonly-spec")){assert(tribes_memory_permissions_set(0x105000,4096,1));call5(12,wt,1,0x105000,0x106000,0x106004);abort();}
 if(argc>1&&!strcmp(argv[1],"wav-short-output")){call5(12,wt,1,0x105000,0x106000,0x10fffe);abort();}
 assert(call5(12,wt,1,0x105000,0x106000,0x106004));
 SDL_AudioSpec expected={SDL_AUDIO_S16LE,1,48000};assert(!memcmp(memory+0x105000,&expected,sizeof expected));
 uint32_t decoded;memcpy(&decoded,memory+0x106000,4);assert(decoded&&*(uint32_t *)(memory+0x106004)==4&&!memcmp(memory+decoded,wav+44,4));
 assert(tribes_global_free((uintptr_t)memory,decoded)==0);
 if(argc>1&&!strcmp(argv[1],"wav-stale")){call(4,wt,0,0,0);abort();}
 /* With closeio=false, the same actual stream remains seekable. */
 wt=(uint32_t)call(11,0x104000,sizeof wav,0,0);assert(call5(12,wt,0,0x105000,0x106000,0x106004));
 memcpy(&decoded,memory+0x106000,4);assert(!memcmp(memory+decoded,wav+44,4));assert(!tribes_global_free((uintptr_t)memory,decoded));assert(call(5,wt,0,0,0)==sizeof wav);assert(call(2,wt,0,0,0));
 memset(memory+0x104000,0,48);wt=(uint32_t)call(11,0x104000,48,0,0);assert(wt);
 memset(memory+0x105000,0xa5,12);memset(memory+0x106000,0xa5,8);
 assert(!call5(12,wt,1,0x105000,0x106000,0x106004));assert(!*(uint32_t *)(memory+0x106000)&&!*(uint32_t *)(memory+0x106004));assert(SDL_GetError()[0]);
 char dir[]="/tmp/tribes-sdl-io-XXXXXX";assert(mkdtemp(dir));assert(tribes_filesystem_configure((uintptr_t)memory,dir,dir,dir));char path[1024];snprintf(path,sizeof path,"%s/Music.dat",dir);FILE *f=fopen(path,"wb");assert(f);assert(fwrite("abc\0def",1,7,f)==7);fclose(f);
 strcpy((char *)memory+0x101000,"music.DAT");strcpy((char *)memory+0x102000,"rb");
 if(argc>1&&!strcmp(argv[1],"unreadable-path")){assert(tribes_memory_permissions_set(0x101000,4096,0));call(1,0x101000,0x102000,0,0);abort();}
 uint32_t token=(uint32_t)call(1,0x101000,0x102000,0,0);assert(token);unlink(path);rmdir(dir); /* Open stream must retain actual file. */
 if(argc>1){
  if(!strcmp(argv[1],"stale")){assert(call(2,token,0,0,0));call(4,token,0,0,0);}
  else if(!strcmp(argv[1],"foreign"))call(4,token+1,0,0,0);
  else if(!strcmp(argv[1],"readonly-output")){assert(tribes_memory_permissions_set(0x103000,4096,1));call(6,token,0x103000,7,0);}
  else if(!strcmp(argv[1],"short-output"))call(6,token,0x10fffd,7,0);
  else if(!strcmp(argv[1],"string-unreadable")){assert(tribes_memory_permissions_set(0x101000,4096,0));call(9,0x101000,0x102000,0,0);}
  else if(!strcmp(argv[1],"string-readonly-output")){assert(tribes_memory_permissions_set(0x103000,4096,1));call(10,0x103000,0x101000,9,0);}
  else if(!strcmp(argv[1],"string-short-output"))call(10,0x10fffe,0x101000,4,0);
  else abort();abort();
 }
 const char *pairs[][2]={{"MP3","mp3"},{"FLAC","mp3"},{"Straße","STRASSE"},{"Σ","ς"},{"Ä","ä"},{"",""},{"a",""},{"\xff","\xfe"}};
 for(unsigned i=0;i<sizeof pairs/sizeof *pairs;i++){
  strcpy((char *)memory+0x104001,pairs[i][0]);strcpy((char *)memory+0x105003,pairs[i][1]);
  assert((int32_t)call(9,0x104001,0x105003,0,0)==SDL_strcasecmp(pairs[i][0],pairs[i][1]));
 }
 memcpy(memory+0x10fffc,"MP3",4);assert(tribes_memory_permissions_set(0x10f000,4096,1));
 strcpy((char *)memory+0x104001,"mp3");assert(!call(9,0x10fffc,0x104001,0,0));
 assert(tribes_memory_permissions_set(0x10f000,4096,3));
 const char *strings[]={"","decoder error","Straße"};
 for(unsigned i=0;i<3;i++)for(unsigned cap=0;cap<20;cap++){
  strcpy((char *)memory+0x104001,strings[i]);memset(memory+0x105000,0xa5,32);char expected[32];memset(expected,0xa5,sizeof expected);
  size_t length=SDL_strlcpy(expected+1,strings[i],cap);
  assert(call(10,cap?0x105001:UINT32_MAX,0x104001,cap,0)==length&&!memcmp(memory+0x105000,expected,32));
 }
 strcpy((char *)memory+0x104001,"abcdef");assert(call(10,0x10fff9,0x104001,UINT32_MAX,0)==6&&!memcmp(memory+0x10fff9,"abcdef",7));
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
 SDL_SetError("%s","shutdown error");assert(SDL_GetError()[0]);assert(call(13,0,0,0,0)==1&&!SDL_GetError()[0]);
 assert(call(2,token,0,0,0));assert(call(1,0x101000,0x102000,0,0)==0);assert(SDL_GetError()[0]);assert(call(4,0,0,0,0)==UINT64_MAX);
 puts("PASS: native UTF-8 comparison and bounded string-copy oracles, protected boundaries, real SDL file bytes, EOF, signed/64-bit seek and size, cdecl CPU/LastError, checked output extent and owned stream lifetime");return 0;
}
