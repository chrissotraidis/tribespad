/* Starsiege's Winsock: TribesPad's native Winsock service (port/core/
 * native_winsock.c, included unchanged so its reviewed hash stays valid) plus
 * the socket options Starsiege sets. The Starsiege build compiles this file in
 * place of native_winsock.c. */
#include "../core/native_winsock.c"

/* int setsockopt(SOCKET, int level, int option, const char *value, int length).
   SO_SNDBUF (0x1001) and SO_RCVBUF (0x1002) apply to the real host socket;
   every other option goes to TribesPad's service. */
uint32_t ss_setsockopt(uint64_t base,uint32_t handle,uint32_t level,uint32_t option,uint32_t value,uint32_t length){
 if(level==0xffff&&(option==0x1001||option==0x1002)){
  int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;
  const void *p=guest(base,value,4);if(!p||length<4||length>INT32_MAX){close(fd);return fail(10014);}
  int32_t bytes;memcpy(&bytes,p,4);
  int result=setsockopt(fd,SOL_SOCKET,option==0x1001?SO_SNDBUF:SO_RCVBUF,&bytes,sizeof bytes),error=errno;close(fd);
  return result?fail(error_number(error)):0;
 }
 return tribes_setsockopt(base,handle,level,option,value,length);
}

