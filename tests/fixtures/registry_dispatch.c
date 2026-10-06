#include <stdint.h>
#include <stdlib.h>
extern void tbimp_RegOpenKeyExA(void),tbimp_RegGetValueA(void);
extern void tbimp_RegOpenKeyExW(void),tbimp_RegQueryValueExW(void),tbimp_RegCloseKey(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){
 if(address==4)return (void *)tbimp_RegOpenKeyExA;
 if(address==5)return (void *)tbimp_RegGetValueA;
 if(address==1)return (void *)tbimp_RegOpenKeyExW;
 if(address==2)return (void *)tbimp_RegQueryValueExW;
 if(address==3)return (void *)tbimp_RegCloseKey;
 if(address==0xfffff000)return (void *)tribes_return_to_host;
 abort();
}
