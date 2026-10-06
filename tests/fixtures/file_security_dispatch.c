#include <stdint.h>
#include <stdlib.h>
extern void tbimp_CreateFileA(void),tbimp_CreateFileW(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){
    if(address==1)return (void *)tbimp_CreateFileA;
    if(address==2)return (void *)tbimp_CreateFileW;
    if(address==0xfffff000)return (void *)tribes_return_to_host;
    abort();
}
_Noreturn void tribes_unimplemented_dinput(void *cpu,uint32_t method){(void)cpu;(void)method;abort();}
