/* Starsiege's Winsock: TribesPad's native Winsock service (port/core/
 * native_winsock.c, included unchanged so its reviewed hash stays valid) plus
 * the socket options Starsiege sets. The Starsiege build compiles this file in
 * place of native_winsock.c. */
#include "../core/native_winsock.c"
#include <arpa/inet.h>

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


/* unsigned long inet_addr(const char *): the host's parser accepts the same
   dotted forms as Windows and returns INADDR_NONE for invalid input; both
   give network byte order in the same four bytes. */
uint32_t ss_inet_addr(uint64_t base,uint32_t text){
 const char *p=guest(base,text,1);if(!p)return UINT32_MAX;
 char copy[64];size_t n=strnlen(p,sizeof copy);if(n==sizeof copy)return UINT32_MAX;memcpy(copy,p,n);copy[n]=0;
 return (uint32_t)inet_addr(copy);
}

/* struct hostent *gethostbyname(const char *name): IPv4 through the host's
   resolver. Windows returns one hostent per thread, valid until that thread's
   next call; the game resolves from one thread at a time (DNet::resolveMasters'
   worker, which copies the address out at once), so one buffer in guest
   memory, filled under a lock, serves. Layout (32-bit): h_name, h_aliases,
   short h_addrtype (AF_INET 2), short h_length (4), h_addr_list; then the
   alias list (empty), up to 8 address pointers, the addresses and the name.
   Failure returns NULL with WSAHOST_NOT_FOUND (11001). */
uint32_t ss_gethostbyname(uint64_t base,uint32_t text){
 static pthread_mutex_t host_lock=PTHREAD_MUTEX_INITIALIZER;static uint32_t buffer;
 const char *p=guest(base,text,1);if(!p){tribes_set_last_error(10014);return 0;}
 char name[256];size_t n=strnlen(p,sizeof name);if(n==sizeof name||!n){tribes_set_last_error(11001);return 0;}
 memcpy(name,p,n);name[n]=0;
 struct addrinfo hints={0},*found=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
 if(getaddrinfo(name,NULL,&hints,&found)||!found){tribes_set_last_error(11001);return 0;}
 pthread_mutex_lock(&host_lock);
 if(!buffer)buffer=tribes_global_alloc(base,0,512);
 if(!buffer){pthread_mutex_unlock(&host_lock);freeaddrinfo(found);tribes_set_last_error(8);return 0;}
 uint32_t aliases=buffer+16,list=buffer+20,addresses=buffer+56,text_at=buffer+88,count=0;
 uint32_t pointers[9]={0};unsigned char *out=guest(base,buffer,512);memset(out,0,512);
 for(struct addrinfo *a=found;a&&count<8;a=a->ai_next){
  if(a->ai_family!=AF_INET||!a->ai_addr)continue;
  memcpy(out+(addresses-buffer)+4*count,&((const struct sockaddr_in *)(const void *)a->ai_addr)->sin_addr,4);
  pointers[count]=addresses+4*count;count++;
 }
 freeaddrinfo(found);
 if(!count){pthread_mutex_unlock(&host_lock);tribes_set_last_error(11001);return 0;}
 memcpy(out+(text_at-buffer),name,n+1);
 uint32_t header[3]={text_at,aliases,2u|(4u<<16)};memcpy(out,header,12);memcpy(out+12,&list,4);
 memcpy(out+(list-buffer),pointers,sizeof pointers);
 pthread_mutex_unlock(&host_lock);
 return buffer;
}
