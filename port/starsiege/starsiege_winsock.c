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

/* int getsockopt(SOCKET, int level, int option, char *value, int *length):
   SO_SNDBUF (0x1001), SO_RCVBUF (0x1002), SO_ERROR (0x1007) and SO_TYPE (0x1008)
   read the real host socket (Tribes: Vengeance's IpDrv asks for its buffer
   sizes); the protocol-info queries go to TribesPad's service. */
uint32_t ss_getsockopt(uint64_t base,uint32_t handle,uint32_t level,uint32_t option,uint32_t value,uint32_t length_pointer){
 if(level==0xffff&&(option==0x1001||option==0x1002||option==0x1007||option==0x1008)){
  void *lengthp=guest(base,length_pointer,4);if(!lengthp)return fail(10014);
  int32_t capacity;memcpy(&capacity,lengthp,4);void *out=guest(base,value,4);if(capacity<4||!out)return fail(10014);
  int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;
  int32_t v=0;socklen_t size=sizeof v;int native=option==0x1001?SO_SNDBUF:option==0x1002?SO_RCVBUF:option==0x1007?SO_ERROR:SO_TYPE;
  int result=getsockopt(fd,SOL_SOCKET,native,&v,&size),error=errno;close(fd);if(result)return fail(error_number(error));
  if(option==0x1007)v=v?(int32_t)error_number(v):0;
  if(option==0x1008)v=v==SOCK_STREAM?1:2;
  memcpy(out,&v,4);int32_t four=4;memcpy(lengthp,&four,4);return 0;
 }
 return tribes_getsockopt(base,handle,level,option,value,length_pointer);
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
static int net_trace(void){static int v=-1;if(v<0)v=getenv("TRIBESPAD_TRACE_NET")!=NULL;return v;}
uint32_t ss_gethostbyname(uint64_t base,uint32_t text){
 static pthread_mutex_t host_lock=PTHREAD_MUTEX_INITIALIZER;static uint32_t buffer;
 const char *p=guest(base,text,1);if(!p){tribes_set_last_error(10014);return 0;}
 char name[256];size_t n=strnlen(p,sizeof name);if(n==sizeof name||!n){tribes_set_last_error(11001);return 0;}
 memcpy(name,p,n);name[n]=0;if(net_trace())fprintf(stderr,"TribesPad net: resolve %s\n",name);
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

/* Tribes: Vengeance's networking (Engine.dll and IpDrv.dll import Winsock 1.1):
   the TCP calls its TcpLink and master-server code uses, plus the helpers. The
   game's UDP play traffic uses TribesPad's sendto/recvfrom. Each works on the
   host socket behind the guest handle, with Windows' error numbers. */
/* int gethostname(char *name, int length) */
uint32_t ss_gethostname(uint64_t base,uint32_t name,uint32_t length){
 char host[256];if(gethostname(host,sizeof host)){return fail(error_number(errno));}host[sizeof host-1]=0;
 size_t n=strlen(host)+1;char *out=guest(base,name,length);if(!out||n>length)return fail(10014);
 memcpy(out,host,n);return 0;
}
/* char *inet_ntoa(struct in_addr): one buffer, as Windows keeps one per thread. */
uint32_t ss_inet_ntoa(uint64_t base,uint32_t address){
 static uint32_t buffer;static pthread_mutex_t text_lock=PTHREAD_MUTEX_INITIALIZER;
 pthread_mutex_lock(&text_lock);if(!buffer)buffer=tribes_global_alloc(base,0,16);
 if(buffer){unsigned char *b=(unsigned char *)&address;snprintf(guest(base,buffer,16),16,"%u.%u.%u.%u",b[0],b[1],b[2],b[3]);}
 pthread_mutex_unlock(&text_lock);return buffer;
}
/* int __WSAFDIsSet(SOCKET, fd_set *): is the handle in the set (u_int count, SOCKET[]). */
uint32_t ss_wsa_fd_is_set(uint64_t base,uint32_t handle,uint32_t set){
 uint32_t count;void *p=guest(base,set,4);if(!p)return 0;memcpy(&count,p,4);if(count>64)count=64;
 const unsigned char *list=guest(base,set+4,count*4);if(!list)return 0;
 for(uint32_t i=0;i<count;i++){uint32_t h;memcpy(&h,list+4*i,4);if(h==handle)return 1;}return 0;
}
/* int connect(SOCKET, const sockaddr *, int): a non-blocking connect in progress
   is WSAEWOULDBLOCK on Windows. */
uint32_t ss_connect(uint64_t base,uint32_t handle,uint32_t pointer,uint32_t length){
 int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;struct sockaddr_in address;
 if(!sockaddr_in_guest(base,pointer,length,&address)){close(fd);return UINT32_MAX;}
 if(net_trace())fprintf(stderr,"TribesPad net: connect %s:%u\n",inet_ntoa(address.sin_addr),ntohs(address.sin_port));
 int result=connect(fd,(struct sockaddr *)&address,sizeof address),error=errno;close(fd);
 if(!result)return 0;return fail(error==EINPROGRESS?10035:error_number(error));
}
/* int send(SOCKET, const char *, int, int flags) and int recv(SOCKET, char *, int, int flags):
   no out-of-band data; recv may peek (MSG_PEEK 2). */
uint32_t ss_send(uint64_t base,uint32_t handle,uint32_t pointer,uint32_t length,uint32_t flags){
 if(flags||length>INT32_MAX)return fail(10045);int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;
 void *p=guest(base,pointer,length);if(!p){close(fd);return fail(10014);}
 ssize_t result=send(fd,p,length,0);int error=errno;close(fd);return result<0?fail(error_number(error)):(uint32_t)result;
}
uint32_t ss_recv(uint64_t base,uint32_t handle,uint32_t pointer,uint32_t length,uint32_t flags){
 if((flags&~2u)||length>INT32_MAX)return fail(10045);int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;
 void *p=guest(base,pointer,length);if(!p){close(fd);return fail(10014);}
 ssize_t result=recv(fd,p,length,flags?MSG_PEEK:0);int error=errno;close(fd);return result<0?fail(error_number(error)):(uint32_t)result;
}
/* int listen(SOCKET, int backlog); int shutdown(SOCKET, int how) */
uint32_t ss_listen(uint64_t base,uint32_t handle,uint32_t backlog){
 int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;
 int result=listen(fd,(int)(backlog>128?128:backlog)),error=errno;close(fd);return result?fail(error_number(error)):0;
}
uint32_t ss_shutdown(uint64_t base,uint32_t handle,uint32_t how){
 if(how>2)return fail(10022);int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;
 int result=shutdown(fd,how==0?SHUT_RD:how==1?SHUT_WR:SHUT_RDWR),error=errno;close(fd);return result?fail(error_number(error)):0;
}
/* SOCKET accept(SOCKET, sockaddr *, int *): the new connection becomes a guest
   socket like socket() makes; INVALID_SOCKET (all ones) on failure. */
uint32_t ss_accept(uint64_t base,uint32_t handle,uint32_t pointer,uint32_t length_pointer){
 int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;struct sockaddr_in address;socklen_t size=sizeof address;memset(&address,0,sizeof address);
 int client=accept(fd,(struct sockaddr *)&address,&size),error=errno;close(fd);if(client<0)return fail(error_number(error));
 int enabled=1;if(fcntl(client,F_SETFD,FD_CLOEXEC)||setsockopt(client,SOL_SOCKET,SO_NOSIGPIPE,&enabled,sizeof enabled)){error=errno;close(client);return fail(error_number(error));}
 if(pointer&&length_pointer){void *nptr=guest(base,length_pointer,4),*out=guest(base,pointer,16);uint32_t capacity=0;if(nptr)memcpy(&capacity,nptr,4);
  if(!nptr||!out||capacity<16){close(client);return fail(10014);}write_address(out,&address);capacity=16;memcpy(nptr,&capacity,4);}
 pthread_mutex_lock(&lock);Socket *s=calloc(1,sizeof *s);if(!s||next_handle>=0xb2000000){free(s);close(client);pthread_mutex_unlock(&lock);return fail(10055);}
 s->base=base;s->handle=next_handle++;s->fd=client;s->type=1;s->family=2;s->next=sockets;sockets=s;uint32_t result=s->handle;pthread_mutex_unlock(&lock);return result;
}
/* sendto with TRIBESPAD_TRACE_NET: each new destination once, then TribesPad's sendto. */
uint32_t ss_sendto(uint64_t base,uint32_t handle,uint32_t pointer,uint32_t length,uint32_t flags,uint32_t destination,uint32_t address_length){
 if(net_trace()){struct sockaddr_in a;const unsigned char *p=guest(base,destination,16);static uint64_t seen[64];static unsigned n;
  if(p&&address_length>=16){memcpy(&a.sin_port,p+2,2);memcpy(&a.sin_addr,p+4,4);uint64_t key=(uint64_t)a.sin_addr.s_addr<<16|a.sin_port;int found=0;
   for(unsigned i=0;i<n;i++)if(seen[i]==key)found=1;
   if(!found&&n<64){seen[n++]=key;const unsigned char *b=guest(base,pointer,length<48?length:48);char text[100]={0};
    for(unsigned i=0;b&&i<(length<48?length:48);i++)text[i]=b[i]>=32&&b[i]<127?(char)b[i]:'.';
    fprintf(stderr,"TribesPad net: sendto %s:%u (%u bytes) %s\n",inet_ntoa(a.sin_addr),ntohs(a.sin_port),length,text);}}}
 return tribes_sendto(base,handle,pointer,length,flags,destination,address_length);
}
