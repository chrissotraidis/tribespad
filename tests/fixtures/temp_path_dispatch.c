#include "../../port/llasm-support/llasm_cpu.h"
#include <stdlib.h>
extern void tbimp_GetTempPathW(void),tbimp_GetTempPath2W(void),tbimp_QueryPerformanceCounter(void),tbimp_QueryPerformanceFrequency(void),tribes_return_to_host(void);
extern void tbimp_GetWindowsDirectoryA(void),tbimp_GetSystemDirectoryA(void),tbimp_SHGetFolderPathA(void);
extern void tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t address) {
    switch(address){case 1:return (void *)tbimp_GetTempPathW;case 2:return (void *)tbimp_GetTempPath2W;case 3:return (void *)tbimp_QueryPerformanceCounter;case 4:return (void *)tbimp_QueryPerformanceFrequency;case 5:return (void *)tbimp_GetWindowsDirectoryA;case 6:return (void *)tbimp_GetSystemDirectoryA;case 7:return (void *)tbimp_SHGetFolderPathA;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}
}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
