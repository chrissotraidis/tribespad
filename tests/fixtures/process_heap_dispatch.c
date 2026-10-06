#include "../../port/llasm-support/llasm_cpu.h"
#include <stdlib.h>
extern void tbimp_HeapCreate(void),tbimp_HeapDestroy(void),tbimp_HeapReAlloc(void);
extern void tbimp_GetProcessHeap(void),tbimp_HeapAlloc(void),tbimp_HeapFree(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address) {
    switch(address){case 1:return (void *)tbimp_GetProcessHeap;case 2:return (void *)tbimp_HeapAlloc;case 3:return (void *)tbimp_HeapFree;case 4:return (void *)tbimp_HeapCreate;case 5:return (void *)tbimp_HeapDestroy;case 6:return (void *)tbimp_HeapReAlloc;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}
}
/* Generator emits unused COM stop wrappers. They are never called here. */
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
