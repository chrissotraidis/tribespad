#include <stdint.h>
#include <stdlib.h>
extern void tbimp_WSAStartup(void),tbimp_WSACleanup(void),tbimp_GetAddrInfoW(void),tbimp_FreeAddrInfoW(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){case 1:return tbimp_WSAStartup;case 2:return tbimp_WSACleanup;case 3:return tbimp_GetAddrInfoW;case 4:return tbimp_FreeAddrInfoW;case 0xfffff000:return tribes_return_to_host;default:abort();}}
