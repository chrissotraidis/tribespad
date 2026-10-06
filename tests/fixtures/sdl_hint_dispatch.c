#include "../../port/core/guest_sdl_backend.h"
#include "../../port/core/guest_heap.h"
#include <assert.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
extern void tbimp_SDL_GetPrimaryDisplay(void),tbimp_SDL_GetDesktopDisplayMode(void);
extern void tbimp_SDL_Init(void),tbimp_SDL_InitSubSystem(void),tbimp_SDL_WasInit(void),tbimp_SDL_QuitSubSystem(void),tbimp_SDL_Quit(void);
extern void tbimp_SDL_SetHint(void),tbimp_SDL_SetHintWithPriority(void),tbimp_SDL_ResetHint(void);
extern void tbimp_SDL_SetMainReady(void),tbimp_SDL_SetLogOutputFunction(void),tbimp_TestLog(void);
extern void tbimp_SDL_GetHint(void),tbimp_SDL_SetAppMetadata(void),tbimp_SDL_SetAppMetadataProperty(void);
extern void tbimp_TestMalloc(void),tbimp_TestCalloc(void),tbimp_TestRealloc(void),tbimp_TestFree(void),tribes_return_to_host(void);
unsigned sdl_test_allocations,sdl_test_frees,sdl_test_live,sdl_test_fail;
uint32_t sdl_test_latest;
void *tribes_lookup(uint32_t pc){
 switch(pc){case 1:return tbimp_SDL_SetHint;case 2:return tbimp_SDL_SetHintWithPriority;case 3:return tbimp_SDL_ResetHint;case 4:return tbimp_SDL_GetHint;
 case 8:return tbimp_SDL_SetMainReady;case 7:return tbimp_SDL_SetLogOutputFunction;case 15:case 16:return tbimp_TestLog;
 case 5:return tbimp_SDL_SetAppMetadata;case 6:return tbimp_SDL_SetAppMetadataProperty;
 case 9:return tbimp_SDL_Init;case 10:return tbimp_SDL_WasInit;case 17:return tbimp_SDL_QuitSubSystem;case 18:return tbimp_SDL_InitSubSystem;case 19:return tbimp_SDL_Quit;
 case 11:return tbimp_TestMalloc;case 12:return tbimp_TestCalloc;case 13:return tbimp_TestRealloc;case 14:return tbimp_TestFree;
 case 20:return tbimp_SDL_GetPrimaryDisplay;case 21:return tbimp_SDL_GetDesktopDisplayMode;
 case 0xfffff000:return tribes_return_to_host;default:abort();}
}
uint32_t sdl_test_malloc(_cpu *c,uint32_t size){
 if(sdl_test_fail){if(sdl_test_fail!=UINT32_MAX)sdl_test_fail=0;return 0;}
 uint32_t p=tribes_global_alloc(c->_pointer_offset,0,size);assert(p);sdl_test_latest=p;sdl_test_allocations++;sdl_test_live++;return p;
}
uint32_t sdl_test_calloc(_cpu *c,uint32_t count,uint32_t size){
 assert(!size||count<=UINT32_MAX/size);uint32_t p=sdl_test_malloc(c,count*size);
 if(p)memset((void *)(uintptr_t)(c->_pointer_offset+p),0,count*size);return p;
}
void sdl_test_free(_cpu *c,uint32_t p){if(p){assert(!tribes_global_free(c->_pointer_offset,p));sdl_test_frees++;sdl_test_live--;}}
uint32_t sdl_test_realloc(_cpu *c,uint32_t p,uint32_t size){
 uint32_t result=sdl_test_malloc(c,size);if(result&&p){uint32_t old=tribes_global_size(c->_pointer_offset,p);
 memcpy((void *)(uintptr_t)(c->_pointer_offset+result),(void *)(uintptr_t)(c->_pointer_offset+p),size<old?size:old);sdl_test_free(c,p);}return result;
}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t index){(void)cpu;(void)index;abort();}

char sdl_test_metadata[4][128];unsigned sdl_test_metadata_present[4];
void sdl_test_metadata_observe(void){
 const char *keys[]={SDL_PROP_APP_METADATA_NAME_STRING,SDL_PROP_APP_METADATA_VERSION_STRING,SDL_PROP_APP_METADATA_IDENTIFIER_STRING,SDL_PROP_APP_METADATA_CREATOR_STRING};
 for(unsigned i=0;i<4;i++){
  const char *value=SDL_GetAppMetadataProperty(keys[i]);sdl_test_metadata_present[i]=value!=NULL;
  if(value){size_t n=strlen(value);assert(n<sizeof sdl_test_metadata[i]);memcpy(sdl_test_metadata[i],value,n+1);}
 }
}

unsigned sdl_test_log_emit,sdl_test_log_count;
uint32_t sdl_test_log_userdata,sdl_test_log_category,sdl_test_log_priority;
char sdl_test_log_message[4096];
void sdl_test_log(_cpu *cpu,uint32_t userdata,uint32_t category,uint32_t priority,uint32_t message){
    sdl_test_log_count++;sdl_test_log_userdata=userdata;sdl_test_log_category=category;sdl_test_log_priority=priority;
    const char *text=(const char *)(uintptr_t)(cpu->_pointer_offset+message);
    size_t length=strlen(text);assert(length<sizeof sdl_test_log_message);memcpy(sdl_test_log_message,text,length+1);
    /* Exercise nested owner entry while the native logger's frame is suspended. */
    strcpy((char *)(uintptr_t)(cpu->_pointer_offset+0x7000),"SDL_LOG_REENTRY");
    strcpy((char *)(uintptr_t)(cpu->_pointer_offset+0x7100),"works");
    assert(tribes_sdl_set_hint(cpu,0x7000,0x7100));tribes_set_last_error(0xabcdef);
}
void sdl_test_log_observe(void){
    if(!sdl_test_log_emit)return;
    if(sdl_test_log_emit==1)SDL_LogMessage(19,SDL_LOG_PRIORITY_ERROR,"%s","native caf\xc3\xa9\r\n");
    else {char message[2049];memset(message,'L',sizeof message-1);message[sizeof message-1]=0;SDL_LogMessage(19,SDL_LOG_PRIORITY_ERROR,"%s",message);}
}
