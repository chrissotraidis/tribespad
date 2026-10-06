#include "../../port/llasm-support/llasm_cpu.h"
#include <stdlib.h>
extern void tbimp_LCMapStringA(void),tbimp_LCMapStringW(void),tbimp_GetStringTypeA(void);
extern void tbimp_GetACP(void),tbimp_GetOEMCP(void),tbimp_GetCPInfo(void),tbimp_IsValidCodePage(void),tbimp_MultiByteToWideChar(void),tbimp_GetStringTypeW(void),tbimp_LCMapStringEx(void),tbimp_WideCharToMultiByte(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address) {
    switch(address){case 1:return (void *)tbimp_GetACP;case 2:return (void *)tbimp_GetOEMCP;case 3:return (void *)tbimp_GetCPInfo;case 4:return (void *)tbimp_IsValidCodePage;case 5:return (void *)tbimp_MultiByteToWideChar;case 6:return (void *)tbimp_GetStringTypeW;case 7:return (void *)tbimp_LCMapStringEx;case 8:return (void *)tbimp_WideCharToMultiByte;case 9:return (void *)tbimp_LCMapStringW;case 10:return (void *)tbimp_LCMapStringA;case 11:return (void *)tbimp_GetStringTypeA;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}
}
/* Generator emits unused COM stop wrappers. They are never called here. */
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
