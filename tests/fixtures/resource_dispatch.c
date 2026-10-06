#include <stdint.h>
#include <stdlib.h>
extern void tbimp_FindResourceA(void),tbimp_SizeofResource(void),tbimp_LoadResource(void),tbimp_LockResource(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){case 1:return (void *)tbimp_FindResourceA;case 2:return (void *)tbimp_SizeofResource;case 3:return (void *)tbimp_LoadResource;case 4:return (void *)tbimp_LockResource;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
