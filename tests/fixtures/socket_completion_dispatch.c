#include <stdint.h>
#include <stdlib.h>
#define X(N,F) extern void tbimp_##F(void);
#define ENTRIES X(1,CreateIoCompletionPort) X(2,PostQueuedCompletionStatus) X(3,GetQueuedCompletionStatus) X(4,CloseHandle) X(5,GetQueuedCompletionStatusEx) X(7,WSAStartup) X(8,WSACleanup) X(9,socket) X(10,closesocket) X(11,SetFileCompletionNotificationModes)
ENTRIES
#undef X
extern void tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){
#define X(N,F) case N:return (void *)tbimp_##F;
ENTRIES
#undef X
case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
