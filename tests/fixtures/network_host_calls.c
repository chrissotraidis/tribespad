#include <sys/resource.h>
#include <sys/socket.h>
#include <stdint.h>
/* Deliberately unsafe direct pointers: every call must stop before the kernel
   sees guest storage. The real service marshals these structures separately. */
int network_guest_pointer(unsigned kind,int fd,void *guest){
    struct sockaddr_storage address={0};socklen_t length=sizeof address;
    switch(kind){
    case 0:return getrlimit(RLIMIT_NOFILE,guest);
    case 1:return setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,guest,4);
    case 2:return bind(fd,guest,sizeof(struct sockaddr));
    case 3:return getsockname(fd,guest,&length);
    case 4:return getsockname(fd,(struct sockaddr *)&address,guest);
    default:return -1;
    }
}
