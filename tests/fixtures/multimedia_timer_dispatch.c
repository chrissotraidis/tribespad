#include <stdint.h>
#include <stdlib.h>
extern void tbimp_timeGetDevCaps(void),tbimp_timeBeginPeriod(void),tbimp_timeEndPeriod(void),tbimp_timeGetTime(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){
 if(pc==1)return (void *)tbimp_timeGetDevCaps;
 if(pc==2)return (void *)tbimp_timeBeginPeriod;
 if(pc==3)return (void *)tbimp_timeEndPeriod;
 if(pc==4)return (void *)tbimp_timeGetTime;
 if(pc==0xfffff000)return (void *)tribes_return_to_host;
 abort();
}
