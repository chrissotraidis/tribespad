#include <stdint.h>
#include <stdlib.h>
extern void tribes_return_to_host(void);
extern void tbimp_SDL_HasMMX(void),tbimp_SDL_HasSSE(void),tbimp_SDL_HasAVX(void);
void *tribes_lookup(uint32_t pc){
 if(pc==1)return (void *)tbimp_SDL_HasMMX;
 if(pc==2)return (void *)tbimp_SDL_HasSSE;
 if(pc==3)return (void *)tbimp_SDL_HasAVX;
 if(pc==0xfffff000)return (void *)tribes_return_to_host;
 abort();
}
