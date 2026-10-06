#include "../../port/core/guest_sdl_tls.h"
#include "../../port/core/guest_heap.h"
#include <assert.h>
#include <stdlib.h>
extern void tbimp_SDL_SetTLS(void),tbimp_SDL_GetTLS(void),tbimp_SDL_CleanupTLS(void),tbimp_TestDestructor(void),tribes_return_to_host(void);
static _Thread_local uint32_t expected,expected_id,callbacks;
static int mutation;
void sdl_tls_expect(uint32_t id,uint32_t value){expected_id=id;expected=value;}
uint32_t sdl_tls_callbacks(void){return callbacks;}
void sdl_tls_mutate(void){mutation=1;}
void sdl_tls_destructor(_cpu *cpu,uint32_t value){
 assert(value==expected);assert(tribes_sdl_get_tls(cpu,expected_id)==value);++callbacks;
 if(mutation)tribes_sdl_set_tls(cpu,expected_id,value,4);
 tribes_set_last_error(0x8888);cpu->_eax=0x12345678;cpu->_ecx=0x87654321;
}
void *tribes_lookup(uint32_t pc){
 switch(pc){case 1:return tbimp_SDL_SetTLS;case 2:return tbimp_SDL_GetTLS;case 3:return tbimp_SDL_CleanupTLS;case 4:return tbimp_TestDestructor;case 0xfffff000:return tribes_return_to_host;default:abort();}
}
