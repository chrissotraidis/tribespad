#include <netdb.h>
#include <stddef.h>
void dns_host_pointer(unsigned kind,void *guest){
 struct addrinfo hints={0},*result=NULL;hints.ai_family=AF_INET;
 switch(kind){case 0:(void)getaddrinfo(guest,"0",&hints,&result);break;case 1:(void)getaddrinfo("127.0.0.1",guest,&hints,&result);break;case 2:(void)getaddrinfo("127.0.0.1","0",guest,&result);break;case 3:(void)getaddrinfo("127.0.0.1","0",&hints,guest);break;case 4:freeaddrinfo(guest);break;}
}
