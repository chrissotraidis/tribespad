#include <stdint.h>
#include <stdlib.h>
extern void tbimp_SetPriorityClass(void),tbimp_GetProcessAffinityMask(void),tbimp_SetThreadAffinityMask(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){
 if(pc==1)return (void *)tbimp_SetPriorityClass;
 if(pc==2)return (void *)tbimp_GetProcessAffinityMask;
 if(pc==3)return (void *)tbimp_SetThreadAffinityMask;
 if(pc==0xfffff000)return (void *)tribes_return_to_host;
 abort();
}
