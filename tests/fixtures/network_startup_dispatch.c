#include <stdint.h>
#include <stdlib.h>
extern void tbimp_setsockopt(void);
extern void tbimp_WSAStartup(void),tbimp_WSACleanup(void),tbimp_socket(void),tbimp_closesocket(void),tbimp_bind(void),tbimp_getsockname(void),tbimp_getsockopt(void),tbimp_SetHandleInformation(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){
case 1:return (void *)tbimp_WSAStartup;case 2:return (void *)tbimp_WSACleanup;
case 3:return (void *)tbimp_socket;case 4:return (void *)tbimp_closesocket;
case 5:return (void *)tbimp_bind;case 6:return (void *)tbimp_getsockname;
case 7:return (void *)tbimp_getsockopt;case 8:return (void *)tbimp_SetHandleInformation;
case 9:return (void *)tbimp_setsockopt;
case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
