#include <stdint.h>
#include <stdlib.h>
extern void tbimp_CreateFileA(void),tbimp_CreateFileW(void);
extern void tbimp_CreateDirectoryA(void),tbimp_CreateDirectoryW(void);
extern void tbimp_GetFileAttributesA(void),tbimp_GetFileAttributesW(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){
 if(address==1)return tbimp_GetFileAttributesA;
 if(address==2)return tbimp_GetFileAttributesW;
 if(address==3)return tbimp_CreateDirectoryA;
 if(address==4)return tbimp_CreateDirectoryW;
 if(address==5)return tbimp_CreateFileA;
 if(address==6)return tbimp_CreateFileW;
 if(address==0xfffff000)return tribes_return_to_host;
 abort();
}
_Noreturn void tribes_unimplemented_dinput(void *cpu,uint32_t method){(void)cpu;(void)method;abort();}
