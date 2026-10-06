#include <stdint.h>
#include <stdlib.h>
extern void tbimp_BCryptGenRandom(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_BCryptGenRandom;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
