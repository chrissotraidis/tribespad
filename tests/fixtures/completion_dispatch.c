#include <stdint.h>
#include <stdlib.h>
extern void tbimp_CreateIoCompletionPort(void),tbimp_PostQueuedCompletionStatus(void),tbimp_GetQueuedCompletionStatus(void),tbimp_CloseHandle(void),tbimp_GetQueuedCompletionStatusEx(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){switch(address){case 1:return (void *)tbimp_CreateIoCompletionPort;case 2:return (void *)tbimp_PostQueuedCompletionStatus;case 3:return (void *)tbimp_GetQueuedCompletionStatus;case 4:return (void *)tbimp_CloseHandle;case 5:return (void *)tbimp_GetQueuedCompletionStatusEx;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
_Noreturn void tribes_unimplemented_dinput(void *cpu,uint32_t method){(void)cpu;(void)method;abort();}
