#include <stdint.h>
#include <stdlib.h>
extern void tbimp_CreateMutexA(void),tbimp_CloseHandle(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){switch(address){case 1:return (void *)tbimp_CreateMutexA;case 2:return (void *)tbimp_CloseHandle;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
_Noreturn void tribes_unimplemented_dinput(void *cpu,uint32_t method){(void)cpu;(void)method;abort();}
