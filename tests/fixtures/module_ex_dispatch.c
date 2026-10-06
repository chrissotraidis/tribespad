#include <stdint.h>
#include <stdlib.h>
extern void tbimp_GetModuleHandleExW(void),tbimp_GetModuleHandleExA(void),tbimp_FreeLibrary(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){case 1:return (void *)tbimp_GetModuleHandleExW;case 2:return (void *)tbimp_GetModuleHandleExA;case 3:return (void *)tbimp_FreeLibrary;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
