#include <stdint.h>
#include <stdlib.h>
extern void tbimp_MonitorFromWindow(void),tbimp_GetMonitorInfoA(void),tbimp_GetMonitorInfoW(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){
 if(pc==1)return (void *)tbimp_MonitorFromWindow;
 if(pc==2)return (void *)tbimp_GetMonitorInfoA;
 if(pc==3)return (void *)tbimp_GetMonitorInfoW;
 if(pc==0xfffff000)return (void *)tribes_return_to_host;
 abort();
}
