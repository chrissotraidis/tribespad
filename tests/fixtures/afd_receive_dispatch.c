#include <stdint.h>
#include <stdlib.h>
#define X(N,F) extern void tbimp_##F(void);
#define ENTRIES X(19,WSARecvFrom) X(20,WSASendTo) X(18,NtDeviceIoControlFile) X(1,WSAStartup) X(2,WSACleanup) X(3,socket) X(4,closesocket) X(5,bind) X(6,ioctlsocket) X(7,WSAIoctl) X(8,CreateIoCompletionPort) X(9,GetQueuedCompletionStatusEx) X(10,GetQueuedCompletionStatus) X(11,CloseHandle) X(12,SetFileCompletionNotificationModes) X(13,setsockopt) X(14,WSARecv) X(15,WSASend) X(16,CancelIoEx) X(17,CancelIo) X(0xfffb1240,ConnectEx)
ENTRIES
#undef X
extern void tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){
#define X(N,F) case N:return (void *)tbimp_##F;
ENTRIES
#undef X
case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
#include <sys/socket.h>
void afd_bad_datagram_pointer(void *guest,unsigned which){
 struct sockaddr_storage address={0};socklen_t size=sizeof address;char byte=0;
 if(which==0)(void)sendto(-1,guest,1,0,(void *)&address,size);
 if(which==1)(void)recvfrom(-1,guest,1,0,(void *)&address,&size);
 if(which==2)(void)sendto(-1,&byte,1,0,guest,size);
 if(which==3)(void)recvfrom(-1,&byte,1,0,(void *)&address,guest);
}
