#include <stdint.h>
#include <stdlib.h>
#define X(N,F) extern void tbimp_##F(void);
#define ENTRIES X(1,WSAStartup) X(2,WSACleanup) X(3,socket) X(4,closesocket) X(5,bind) X(6,ioctlsocket) X(7,WSAIoctl) X(8,CreateIoCompletionPort) X(9,GetQueuedCompletionStatusEx) X(10,GetQueuedCompletionStatus) X(11,CloseHandle) X(12,SetFileCompletionNotificationModes) X(13,setsockopt) X(14,WSARecv) X(15,WSASend) X(16,CancelIoEx) X(17,CancelIo) X(0xfffb1240,ConnectEx)
ENTRIES
#undef X
extern void tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){
#define X(N,F) case N:return (void *)tbimp_##F;
ENTRIES
#undef X
case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
