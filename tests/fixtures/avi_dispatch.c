#include <stdint.h>
#include <stdlib.h>
extern void tbimp_AVIFileInit(void),tbimp_AVIFileExit(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){case 1:return tbimp_AVIFileInit;case 2:return tbimp_AVIFileExit;case 0xfffff000:return tribes_return_to_host;default:abort();}}
