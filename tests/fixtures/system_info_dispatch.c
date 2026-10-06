#include <stdint.h>
#include <stdlib.h>
extern void tbimp_GetSystemInfo(void),tbimp_GetNativeSystemInfo(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_GetSystemInfo;if(address==2)return (void *)tbimp_GetNativeSystemInfo;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
