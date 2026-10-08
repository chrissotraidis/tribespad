/* Winsock 1.1 negotiation and native BSD IPv4 sockets; no guest executable pages.
   https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-wsastartup
   Async window notifications and authenticated protocol acceptance remain gates. */
#include "native_winsock.h"
#include "guest_heap.h"
#include "guest_encoding.h"
#include "native_handles.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
uint32_t tribes_network_u16(uint32_t value) {return __builtin_bswap16((uint16_t)value);}
uint32_t tribes_network_u32(uint32_t value) {return __builtin_bswap32(value);}
typedef struct Context { uint64_t base;uint32_t references;struct Context *next; } Context;
typedef struct ConnectOperation ConnectOperation;
typedef struct SocketIo SocketIo;
typedef struct Socket { uint64_t base;uint32_t handle;int fd,closed,connect_done,connect_context;uint32_t type,family,completion_modes;const void *owner;TribesCompletionBinding *completion;ConnectOperation *connecting;SocketIo *reading,*write_head,*write_tail;struct Socket *next; } Socket;
/* Standalone network fixtures do not need queues. Full-engine association
   requires all three strong, checked queue functions to be linked. */
__attribute__((weak,noinline)) TribesCompletionBinding *tribes_completion_binding_create(uint32_t port,uint32_t key){
    (void)port;(void)key;tribes_set_last_error(50);return NULL;
}
__attribute__((weak,noinline)) void tribes_completion_binding_release(TribesCompletionBinding *binding){
    (void)binding;fputs("TribesPad: completion binding provider is not linked\n",stderr);abort();
}
__attribute__((weak,noinline)) uint32_t tribes_completion_binding_post(TribesCompletionBinding *binding,uint32_t bytes,uint32_t overlapped){
    (void)binding;(void)bytes;(void)overlapped;fputs("TribesPad: completion binding provider is not linked\n",stderr);abort();
}
__attribute__((weak,noinline)) uint32_t tribes_completion_binding_post_io(TribesCompletionBinding *binding,uint64_t base,uint32_t bytes,uint32_t overlapped,uint32_t status,uint32_t error){
    (void)binding;(void)base;(void)bytes;(void)overlapped;(void)status;(void)error;
    fputs("TribesPad: I/O completion provider is not linked\n",stderr);abort();
}
static Context *contexts;
static Socket *sockets;
typedef struct AddressList { uint64_t base;uint32_t address;struct AddressList *next; } AddressList;
static AddressList *address_lists;
static uint32_t next_handle=0xb1000001;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t pending_connects,pending_io;
static void free_socket(Socket *s){if(s->completion)tribes_completion_binding_release(s->completion);free(s);}
/* Called with the socket lock. A pending native operation owns the retired
   socket/binding until its cancellation packet has been published. */
static void retire_socket(Socket *s){s->closed=1;if(!s->connecting&&!s->reading&&!s->write_head)free_socket(s);}
static _Noreturn void ownership_stop(void) { fputs("TribesPad: unsupported borrowed socket ownership\n",stderr);abort(); }
static Context *find(uint64_t base) { for(Context *c=contexts;c;c=c->next)if(c->base==base)return c;return NULL; }
static uint32_t error_number(int error) {
    switch(error){case EINTR:return 10004;case EACCES:case EPERM:return 10013;case EFAULT:return 10014;case EINVAL:return 10022;case EMFILE:case ENFILE:return 10024;case EAGAIN:return 10035;case EINPROGRESS:return 10036;case EALREADY:return 10037;case EBADF:case ENOTSOCK:return 10038;case EDESTADDRREQ:return 10039;case EMSGSIZE:return 10040;case EPROTOTYPE:return 10041;case ENOPROTOOPT:return 10042;case EPROTONOSUPPORT:return 10043;case ESOCKTNOSUPPORT:return 10044;case EOPNOTSUPP:return 10045;case EAFNOSUPPORT:return 10047;case EADDRINUSE:return 10048;case EADDRNOTAVAIL:return 10049;case ENETDOWN:return 10050;case ENETUNREACH:return 10051;case ENETRESET:return 10052;case ECONNABORTED:return 10053;case ECONNRESET:case EPIPE:return 10054;case ENOBUFS:case ENOMEM:return 10055;case EISCONN:return 10056;case ENOTCONN:return 10057;case ESHUTDOWN:return 10058;case ETIMEDOUT:return 10060;case ECONNREFUSED:return 10061;case EHOSTDOWN:return 10064;case EHOSTUNREACH:return 10065;default:return 10022;}
}
static uint32_t fail(uint32_t error) { tribes_set_last_error(error);return UINT32_MAX; }
static void *guest(uint64_t base,uint32_t pointer,uint32_t size) {
    if((size&&!pointer)||(size&&pointer>UINT32_MAX-(size-1))||base>UINTPTR_MAX-pointer-(size?size-1:0))return NULL;
    return (void *)(uintptr_t)(base+pointer);
}
static uint32_t dns_error(uint32_t error){tribes_set_last_error(error);return error;}
static uint32_t resolver_error(int error){
    switch(error){
    case EAI_AGAIN:return 11002;case EAI_BADFLAGS:return 10022;case EAI_FAIL:return 11003;
    case EAI_FAMILY:return 10047;case EAI_MEMORY:return 8;case EAI_NONAME:return 11001;
    case EAI_NODATA:return 11004;case EAI_SERVICE:return 10109;case EAI_SOCKTYPE:return 10044;
    case EAI_SYSTEM:return error_number(errno);default:return 11003;
    }
}
/* Use the existing checked UTF16 codec, then give the resolver private UTF8.
   Its temporary guest allocation is never retained by the native resolver. */
static char *dns_text(uint64_t base,uint32_t input,uint32_t *error){
    if(!input)return NULL;
    uint32_t length=tribes_wide_to_multibyte(base,65001,0x80,input,UINT32_MAX,0,0,0,0);
    if(!length){*error=10022;return NULL;}
    uint32_t temp=tribes_global_alloc(base,0,length);char *text=malloc(length);
    if(!temp||!text){if(temp)tribes_global_free(base,temp);free(text);*error=8;return NULL;}
    if(tribes_wide_to_multibyte(base,65001,0x80,input,UINT32_MAX,temp,length,0,0)!=length){
        tribes_global_free(base,temp);free(text);*error=10022;return NULL;
    }
    memcpy(text,guest(base,temp,length),length);tribes_global_free(base,temp);return text;
}
static int dns_flags(uint32_t value,int *native){
    if(value&~UINT32_C(0xd0f))return 0;
    *native=0;
    if(value&1)*native|=AI_PASSIVE;if(value&2)*native|=AI_CANONNAME;
    if(value&4)*native|=AI_NUMERICHOST;if(value&8)*native|=AI_NUMERICSERV;
    if(value&0x100)*native|=AI_ALL;if(value&0x400)*native|=AI_ADDRCONFIG;
    if(value&0x800)*native|=AI_V4MAPPED;
    return 1;
}
static uint32_t windows_dns_flags(int value){
    uint32_t result=0;
    if(value&AI_PASSIVE)result|=1;if(value&AI_CANONNAME)result|=2;
    if(value&AI_NUMERICHOST)result|=4;if(value&AI_NUMERICSERV)result|=8;
    if(value&AI_ALL)result|=0x100;if(value&AI_ADDRCONFIG)result|=0x400;
    if(value&AI_V4MAPPED)result|=0x800;
    return result;
}
uint32_t tribes_get_addr_info_w(uint64_t base,uint32_t node,uint32_t service,uint32_t hints,uint32_t output){
    void *destination=guest(base,output,4);if(!destination)return dns_error(10014);
    uint32_t zero=0;memcpy(destination,&zero,4);
    pthread_mutex_lock(&lock);Context *context=find(base);int initialized=context&&context->references;
    pthread_mutex_unlock(&lock);if(!initialized)return dns_error(10093);
    uint32_t words[8]={0};if(hints){void *p=guest(base,hints,32);if(!p)return dns_error(10014);memcpy(words,p,32);}
    if(words[4]||words[5]||words[6]||words[7])return dns_error(11003);
    struct addrinfo native_hints={0};
    if(!dns_flags(words[0],&native_hints.ai_flags))return dns_error(10022);
    if(words[1]!=0&&words[1]!=2&&words[1]!=23)return dns_error(10047);
    if(words[2]>2)return dns_error(10044);
    native_hints.ai_family=words[1]==23?AF_INET6:words[1]==2?AF_INET:AF_UNSPEC;
    native_hints.ai_socktype=words[2]==1?SOCK_STREAM:words[2]==2?SOCK_DGRAM:0;
    native_hints.ai_protocol=(int)words[3];
    uint32_t error=0;char *host=dns_text(base,node,&error),*port=error?NULL:dns_text(base,service,&error);
    if(error){free(host);free(port);return dns_error(error);}
    struct addrinfo *native=NULL;int status=getaddrinfo(host,port,&native_hints,&native);int saved_errno=errno;
    free(host);free(port);errno=saved_errno;if(status)return dns_error(resolver_error(status));
    uint64_t bytes=0;unsigned count=0;
    for(struct addrinfo *a=native;a;a=a->ai_next){
        if((a->ai_family!=AF_INET&&a->ai_family!=AF_INET6)||!a->ai_addr||
           a->ai_addrlen!=(a->ai_family==AF_INET?sizeof(struct sockaddr_in):sizeof(struct sockaddr_in6))){error=11003;break;}
        size_t name=a->ai_canonname?strlen(a->ai_canonname)+1:0;
        bytes+=32+(a->ai_family==AF_INET?16:28)+((name+3)&~UINT64_C(3))+((2ull*name+3)&~UINT64_C(3));
        if(++count>4096||bytes>UINT32_MAX){error=8;break;}
    }
    if(!count&&!error)error=11004;
    AddressList *owned=error?NULL:calloc(1,sizeof *owned);
    uint32_t address=owned?tribes_global_alloc(base,0,(uint32_t)bytes):0;
    if(!owned||!address){if(!error)error=8;free(owned);freeaddrinfo(native);return dns_error(error);}
    uint32_t offset=0;
    for(struct addrinfo *a=native;a;a=a->ai_next){
        uint32_t size=a->ai_family==AF_INET?16:28;
        uint32_t name=a->ai_canonname?(uint32_t)strlen(a->ai_canonname)+1:0;
        uint32_t stride=32+size+((name+3)&~3u)+((2u*name+3)&~3u);
        uint32_t canon=name?address+offset+32+size+((name+3)&~3u):0;
        uint32_t record[8]={windows_dns_flags(a->ai_flags),a->ai_family==AF_INET?2u:23u,
            (uint32_t)a->ai_socktype,(uint32_t)a->ai_protocol,size,canon,address+offset+32,a->ai_next?address+offset+stride:0};
        unsigned char socket_address[28]={0};uint16_t family=(uint16_t)record[1];memcpy(socket_address,&family,2);
        if(a->ai_family==AF_INET){const struct sockaddr_in *s=(const void *)a->ai_addr;
            memcpy(socket_address+2,&s->sin_port,2);memcpy(socket_address+4,&s->sin_addr,4);
        }else{const struct sockaddr_in6 *s=(const void *)a->ai_addr;
            memcpy(socket_address+2,&s->sin6_port,2);memcpy(socket_address+4,&s->sin6_flowinfo,4);
            memcpy(socket_address+8,&s->sin6_addr,16);memcpy(socket_address+24,&s->sin6_scope_id,4);
        }
        memcpy(guest(base,address+offset,32),record,32);memcpy(guest(base,address+offset+32,size),socket_address,size);
        if(name){uint32_t utf8=address+offset+32+size;memcpy(guest(base,utf8,name),a->ai_canonname,name);
            if(!tribes_multibyte_to_wide(base,65001,8,utf8,UINT32_MAX,canon,name)){error=11003;break;}
        }
        offset+=stride;
    }
    freeaddrinfo(native);
    if(error){tribes_global_free(base,address);free(owned);return dns_error(error);}
    owned->base=base;owned->address=address;pthread_mutex_lock(&lock);
    owned->next=address_lists;address_lists=owned;pthread_mutex_unlock(&lock);
    memcpy(destination,&address,4);return dns_error(0);
}
void tribes_free_addr_info_w(uint64_t base,uint32_t address){
    if(!address)return;uint32_t error=tribes_get_last_error();pthread_mutex_lock(&lock);
    AddressList **slot=&address_lists;while(*slot&&((*slot)->base!=base||(*slot)->address!=address))slot=&(*slot)->next;
    if(!*slot){pthread_mutex_unlock(&lock);fputs("TribesPad: stale or foreign DNS result\n",stderr);abort();}
    AddressList *owned=*slot;*slot=owned->next;pthread_mutex_unlock(&lock);
    /* Public links may be changed by guest code; free only private ownership. */
    if(tribes_global_free(base,address)){fputs("TribesPad: DNS result storage was freed outside FreeAddrInfoW\n",stderr);abort();}
    free(owned);tribes_set_last_error(error);
}
uint32_t tribes_wsa_startup(uint64_t base,uint32_t version,uint32_t output) {
    unsigned major=version&255,minor=(version>>8)&255;if(!major||(major==1&&minor<1))return 10092;
    void *buffer=guest(base,output,400);if(!buffer)return 10014;
    int probe=socket(AF_INET,SOCK_DGRAM,0);if(probe<0)return 10091;close(probe);
    struct rlimit limits;if(getrlimit(RLIMIT_NOFILE,&limits))return 10091;
    pthread_mutex_lock(&lock);Context *c=find(base);if(!c){c=calloc(1,sizeof *c);if(!c){pthread_mutex_unlock(&lock);return 10055;}c->base=base;c->next=contexts;contexts=c;}
    if(c->references==UINT32_MAX){pthread_mutex_unlock(&lock);return 10067;}c->references++;pthread_mutex_unlock(&lock);
    unsigned char data[400]={0};uint16_t negotiated=0x101,maximum=(uint16_t)(limits.rlim_cur>UINT16_MAX?UINT16_MAX:limits.rlim_cur),udp=65507;
    memcpy(data,&negotiated,2);memcpy(data+2,&negotiated,2);strcpy((char *)data+4,"TribesPad native BSD sockets");strcpy((char *)data+261,"Running");memcpy(data+390,&maximum,2);memcpy(data+392,&udp,2);memcpy(buffer,data,400);return 0;
}
uint32_t tribes_wsa_cleanup(uint64_t base) {
    pthread_mutex_lock(&lock);Context *c=find(base);if(!c||!c->references){pthread_mutex_unlock(&lock);return fail(10093);}
    if(c->references==1)for(Socket *s=sockets;s;s=s->next)if(s->base==base&&s->owner)ownership_stop();
    c->references--;
    if(!c->references){Socket **p=&sockets;while(*p){Socket *s=*p;if(s->base!=base){p=&s->next;continue;}*p=s->next;shutdown(s->fd,SHUT_RDWR);close(s->fd);s->fd=-1;retire_socket(s);}}
    pthread_mutex_unlock(&lock);return 0;
}
static int borrow(uint64_t base,uint32_t handle) {
    pthread_mutex_lock(&lock);Context *c=find(base);if(!c||!c->references){pthread_mutex_unlock(&lock);fail(10093);return -1;}
    Socket *s=sockets;while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){pthread_mutex_unlock(&lock);fail(10038);return -1;}int copy=fcntl(s->fd,F_DUPFD_CLOEXEC,3);int error=errno;pthread_mutex_unlock(&lock);
    if(copy<0)fail(error_number(error));return copy;
}
int tribes_socket_duplicate(uint64_t base,uint32_t handle) { return borrow(base,handle); }
/* Lock order is socket registry, then queue registry. Queue association never
   holds the queue lock while entering this provider. */
uint32_t tribes_socket_associate_completion(uint64_t base,uint32_t handle,uint32_t port,uint32_t key){
    pthread_mutex_lock(&lock);Socket *s=sockets;
    while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
    if(s->completion){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
    s->completion=tribes_completion_binding_create(port,key);
    uint32_t result=s->completion?port:0;pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_socket_set_completion_modes(uint64_t base,uint32_t handle,uint32_t flags){
    flags&=255u;
    if(flags&~3u){tribes_set_last_error(87);return 0;}
    pthread_mutex_lock(&lock);Socket *s=sockets;
    while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
    /* Notification flags are additive and cannot be cleared. No implicit
       socket-handle event exists; explicit OVERLAPPED events remain separate. */
    s->completion_modes|=flags;pthread_mutex_unlock(&lock);return 1;
}
/* Native operation adapters publish only after real successful I/O. Immediate
   success obeys the configured skip flag; this is not a guest I/O API. */
uint32_t tribes_socket_post_completion(uint64_t base,uint32_t handle,uint32_t bytes,uint32_t overlapped,uint32_t immediate){
    pthread_mutex_lock(&lock);Socket *s=sockets;
    while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s||!s->completion){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
    uint32_t result=immediate&&(s->completion_modes&1u)?1u:tribes_completion_binding_post(s->completion,bytes,overlapped);
    pthread_mutex_unlock(&lock);return result;
}
/* Reached SetHandleInformation contract: change only socket inheritance.
   Descriptor flags belong to the original descriptor, not a poll duplicate.
   Other object classes and close protection remain explicit boundaries. */
uint32_t tribes_set_socket_handle_information(uint64_t base,uint32_t handle,uint32_t mask,uint32_t flags) {
    if(mask&~1u){fputs("TribesPad: unsupported handle-information mask\n",stderr);abort();}
    if(handle<0xb1000001u||handle>=0xb2000000u){fputs("TribesPad: unsupported handle-information object\n",stderr);abort();}
    pthread_mutex_lock(&lock);Socket *s=sockets;
    while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
    int current=fcntl(s->fd,F_GETFD),status=current<0?-1:0;
    if(current>=0&&(mask&1u))status=fcntl(s->fd,F_SETFD,(flags&1u)?current&~FD_CLOEXEC:current|FD_CLOEXEC);
    int error=errno;pthread_mutex_unlock(&lock);
    if(status<0){tribes_set_last_error(error==ENOMEM?8:6);return 0;}
    return 1;
}
uint32_t tribes_socket_attach(uint64_t base,int fd,const void *owner) {
    if(!owner)ownership_stop();
    pthread_mutex_lock(&lock);Context *c=find(base);
    if(!c||!c->references){pthread_mutex_unlock(&lock);return fail(10093);}
    for(Socket *s=sockets;s;s=s->next)if(s->fd==fd){
        if(s->base!=base||s->owner!=owner)ownership_stop();
        uint32_t handle=s->handle;pthread_mutex_unlock(&lock);return handle;
    }
    struct sockaddr_storage address; socklen_t length=sizeof address;
    int type=0;socklen_t size=sizeof type;
    if(getsockname(fd,(struct sockaddr *)&address,&length)||getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size)){
        int error=errno;pthread_mutex_unlock(&lock);return fail(error_number(error));
    }
    if(address.ss_family!=AF_INET&&address.ss_family!=AF_INET6){pthread_mutex_unlock(&lock);return fail(10047);}
    /* The current native curl profile is HTTP/1.1 over TCP. */
    if(type!=SOCK_STREAM){pthread_mutex_unlock(&lock);return fail(10044);}
    Socket *s=calloc(1,sizeof *s);
    if(!s||next_handle>=0xb2000000){free(s);pthread_mutex_unlock(&lock);return fail(10055);}
    s->base=base;s->handle=next_handle++;s->fd=fd;s->type=1;
    s->family=address.ss_family==AF_INET?2:23;s->owner=owner;s->next=sockets;sockets=s;
    uint32_t result=s->handle;pthread_mutex_unlock(&lock);return result;
}
int tribes_socket_detach(uint64_t base,uint32_t handle,const void *owner) {
    pthread_mutex_lock(&lock);Socket **slot=&sockets;
    while(*slot&&((*slot)->base!=base||(*slot)->handle!=handle))slot=&(*slot)->next;
    if(!owner||!*slot||(*slot)->owner!=owner)ownership_stop();
    Socket *s=*slot;*slot=s->next;int fd=s->fd;s->fd=-1;retire_socket(s);pthread_mutex_unlock(&lock);return fd;
}
uint32_t tribes_socket(uint64_t base,uint32_t family,uint32_t type,uint32_t protocol) {
    pthread_mutex_lock(&lock);Context *c=find(base);if(!c||!c->references){pthread_mutex_unlock(&lock);return fail(10093);}
    if(family!=2&&family!=23){pthread_mutex_unlock(&lock);return fail(10047);}if(type!=1&&type!=2){pthread_mutex_unlock(&lock);return fail(10044);}if(protocol&&protocol!=(type==1?6u:17u)){pthread_mutex_unlock(&lock);return fail(10043);}
    int fd=socket(family==23?AF_INET6:AF_INET,type==1?SOCK_STREAM:SOCK_DGRAM,(int)protocol);if(fd<0){int error=errno;pthread_mutex_unlock(&lock);return fail(error_number(error));}
    int enabled=1;if(fcntl(fd,F_SETFD,FD_CLOEXEC)||setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&enabled,sizeof enabled)){int error=errno;close(fd);pthread_mutex_unlock(&lock);return fail(error_number(error));}
    /* Windows IPv6 sockets start IPv6-only; the original UDP setup explicitly
       clears IPV6_V6ONLY before binding for IPv4-mapped community traffic. */
    if(family==23&&setsockopt(fd,IPPROTO_IPV6,IPV6_V6ONLY,&enabled,sizeof enabled)){int error=errno;close(fd);pthread_mutex_unlock(&lock);return fail(error_number(error));}
    Socket *s=calloc(1,sizeof *s);if(!s||next_handle>=0xb2000000){free(s);close(fd);pthread_mutex_unlock(&lock);return fail(10055);}
    s->base=base;s->handle=next_handle++;s->fd=fd;s->type=type;s->family=family;s->next=sockets;sockets=s;uint32_t result=s->handle;pthread_mutex_unlock(&lock);return result;
}
/* Native provider description, with Windows' 32-bit WSAPROTOCOL_INFO layout.
   The descriptors are private socket handles, not NT IFS/file handles. Keep
   XP1_IFS_HANDLES clear so callers choose their non-IFS networking path.
   Transport flags describe the actual BSD TCP/datagram protocol. This does
   not imply that every Winsock operation has already been bridged.
   https://learn.microsoft.com/windows/win32/api/winsock2/ns-winsock2-wsaprotocol_infow */
typedef struct {
    uint32_t service[4],provider_flags;unsigned char provider_id[16];
    uint32_t catalog,chain_length,chain_entries[7];
    uint32_t version,family,max_address,min_address,type,protocol,max_offset,byte_order,security;
    uint32_t message_size,reserved;uint16_t name[256];
} ProtocolInfoW;
_Static_assert(sizeof(ProtocolInfoW)==628,"WSAPROTOCOL_INFOW size");
_Static_assert(offsetof(ProtocolInfoW,name)==116,"WSAPROTOCOL_INFO name offset");
uint32_t tribes_getsockopt(uint64_t base,uint32_t handle,uint32_t level,uint32_t option,uint32_t output,uint32_t length_pointer) {
    pthread_mutex_lock(&lock);Context *c=find(base);
    if(!c||!c->references){pthread_mutex_unlock(&lock);return fail(10093);}
    Socket *s=sockets;while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){pthread_mutex_unlock(&lock);return fail(10038);}
    uint32_t type=s->type,family=s->family;pthread_mutex_unlock(&lock);
    if(level!=0xffff||(option!=0x2004&&option!=0x2005)){
        fprintf(stderr,"TribesPad: unsupported getsockopt level=0x%x option=0x%x\n",level,option);abort();
    }
    void *lengthp=guest(base,length_pointer,4);if(!lengthp)return fail(10014);
    int32_t capacity;memcpy(&capacity,lengthp,4);uint32_t required=option==0x2005?628:372;
    void *destination=guest(base,output,required);
    if(capacity<(int32_t)required||!destination){memcpy(lengthp,&required,4);return fail(10014);}
    ProtocolInfoW info={0};
    /* Own stable provider identity, never a Microsoft/AFD provider GUID. */
    static const unsigned char guid[16]={0x22,0xbb,0x38,0x56,0xd4,0xac,0xe7,0x58,0x8a,0x65,0xe1,0x82,0xfb,0xcc,0x38,0xfa};
    memcpy(info.provider_id,guid,16);info.service[0]=type==1?0x26:9;
    info.provider_flags=8;info.catalog=type+(family==23?2:0);info.chain_length=1;info.version=2;
    info.family=family;info.max_address=family==23?28:16;info.min_address=info.max_address;info.type=type;
    info.protocol=type==1?6:17;info.message_size=type==1?0:65507;
    const char *name=family==23?(type==1?"TribesPad native TCP/IPv6":"TribesPad native UDP/IPv6"):(type==1?"TribesPad native TCP/IPv4":"TribesPad native UDP/IPv4");
    unsigned char packed[628]={0};memcpy(packed,&info,116);
    for(unsigned i=0;name[i];i++){
        if(option==0x2005){uint16_t unit=(unsigned char)name[i];memcpy(packed+116+2*i,&unit,2);}
        else packed[116+i]=(unsigned char)name[i];
    }
    memcpy(destination,packed,required);memcpy(lengthp,&required,4);return 0;
}
uint32_t tribes_closesocket(uint64_t base,uint32_t handle) {
    pthread_mutex_lock(&lock);Context *c=find(base);if(!c||!c->references){pthread_mutex_unlock(&lock);return fail(10093);}Socket **p=&sockets;while(*p&&((*p)->base!=base||(*p)->handle!=handle))p=&(*p)->next;
    if(!*p){pthread_mutex_unlock(&lock);return fail(10038);}Socket *s=*p;if(s->owner)ownership_stop();*p=s->next;shutdown(s->fd,SHUT_RDWR);int result=close(s->fd),error=errno;s->fd=-1;retire_socket(s);pthread_mutex_unlock(&lock);return result?fail(error_number(error)):0;
}
static int sockaddr_in_guest(uint64_t base,uint32_t pointer,uint32_t length,struct sockaddr_in *address) {
    const unsigned char *p=guest(base,pointer,16);if(!p||length<16||length>INT32_MAX){fail(10014);return 0;}uint16_t family;memcpy(&family,p,2);if(family!=2){fail(10047);return 0;}
    memset(address,0,sizeof *address);address->sin_len=sizeof *address;address->sin_family=AF_INET;memcpy(&address->sin_port,p+2,2);memcpy(&address->sin_addr,p+4,4);return 1;
}
static void write_address(void *output,const struct sockaddr_in *address) { unsigned char data[16]={2,0};memcpy(data+2,&address->sin_port,2);memcpy(data+4,&address->sin_addr,4);memcpy(output,data,16); }
static int socket_address_guest(uint64_t base,uint32_t pointer,uint32_t length,struct sockaddr_storage *address,socklen_t *native_length){
    const unsigned char *p=guest(base,pointer,2);uint16_t family;
    if(!p||length<2||length>INT32_MAX){fail(10014);return 0;}memcpy(&family,p,2);
    memset(address,0,sizeof *address);
    if(family==2){*native_length=sizeof(struct sockaddr_in);return sockaddr_in_guest(base,pointer,length,(struct sockaddr_in *)address);}
    if(family!=23){fail(10047);return 0;}
    if(length<28||!(p=guest(base,pointer,28))){fail(10014);return 0;}
    struct sockaddr_in6 *v=(struct sockaddr_in6 *)address;v->sin6_len=sizeof *v;v->sin6_family=AF_INET6;
    memcpy(&v->sin6_port,p+2,2);memcpy(&v->sin6_flowinfo,p+4,4);memcpy(&v->sin6_addr,p+8,16);memcpy(&v->sin6_scope_id,p+24,4);
    *native_length=sizeof *v;return 1;
}
uint32_t tribes_bind_socket(uint64_t base,uint32_t handle,uint32_t pointer,uint32_t length) {
    int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;struct sockaddr_storage address;socklen_t n;
    if(!socket_address_guest(base,pointer,length,&address,&n)){close(fd);return UINT32_MAX;}
    int result=bind(fd,(struct sockaddr *)&address,n),error=errno;close(fd);return result?fail(error_number(error)):0;
}
uint32_t tribes_getsockname(uint64_t base,uint32_t handle,uint32_t pointer,uint32_t length_pointer) {
    int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;void *lengthp=guest(base,length_pointer,4);int32_t capacity=0;if(lengthp)memcpy(&capacity,lengthp,4);
    if(!lengthp||capacity<16){close(fd);return fail(10014);}
    struct sockaddr_storage address;socklen_t n=sizeof address;int result=getsockname(fd,(struct sockaddr *)&address,&n),error=errno;close(fd);
    if(result)return fail(error_number(error));
    unsigned char packed[28]={0};uint32_t length;
    if(address.ss_family==AF_INET&&n>=sizeof(struct sockaddr_in)) { length=16;write_address(packed,(const struct sockaddr_in *)&address); }
    else if(address.ss_family==AF_INET6&&n>=sizeof(struct sockaddr_in6)) {
        length=28;uint16_t family=23;memcpy(packed,&family,2);memcpy(packed+2,(const unsigned char *)&address+2,26);
    } else return fail(10047);
    void *p=guest(base,pointer,length);if(!p||capacity<(int32_t)length)return fail(10014);
    memcpy(p,packed,length);memcpy(lengthp,&length,4);return 0;
}
/* AOT guest threads execute one-to-one on native threads. Keep the native
   issuer ID private and stable across pthread storage reuse. */
static uint64_t io_issuer(void){uint64_t id=0;if(pthread_threadid_np(NULL,&id)||!id){fputs("TribesPad: native socket issuer identity failed\n",stderr);abort();}return id;}
struct ConnectOperation {Socket *socket;int fd,canceled;uint32_t overlapped;uint64_t issuer;struct sockaddr_in address;};
/* Translate actual Darwin connect failures into the statuses consumed by
   original libuv. Other real Winsock errors use its NTWIN32 fallback. */
static uint32_t socket_status(int error,uint32_t *dos){
    if(!error){*dos=0;return 0;}
    switch(error){
    case ECANCELED:*dos=995;return 0xc0000120u;
    case ECONNREFUSED:*dos=1225;return 0xc0000236u;
    case ECONNRESET:case EPIPE:*dos=64;return 0xc000020du;
    case ECONNABORTED:*dos=1236;return 0xc0000241u;
    case ETIMEDOUT:*dos=121;return 0xc00000b5u;
    case ENETUNREACH:*dos=1231;return 0xc000023cu;
    case EHOSTUNREACH:*dos=1232;return 0xc000023du;
    default:*dos=error_number(error);return 0xc0070000u|*dos;
    }
}
static void *run_connect(void *opaque){
    ConnectOperation *op=opaque;Socket *s=op->socket;int error=0;
    pthread_mutex_lock(&lock);int canceled=s->closed||op->canceled;pthread_mutex_unlock(&lock);
    if(canceled)error=ECANCELED;
    else{int result=connect(op->fd,(const struct sockaddr *)&op->address,sizeof op->address);error=result?errno:0;}
#ifdef TB_SOCKET_CONNECT_OBSERVATION
    extern void tribes_socket_connect_observation(void);tribes_socket_connect_observation();
#endif
    while(error==EINPROGRESS||error==EALREADY||error==EINTR){
        pthread_mutex_lock(&lock);canceled=s->closed||op->canceled;pthread_mutex_unlock(&lock);
        if(canceled){error=ECANCELED;break;}
        struct pollfd p={op->fd,POLLOUT,0};int ready=poll(&p,1,50);
        if(ready<0){if(errno==EINTR)continue;error=errno;break;}
        if(!ready)continue;
        socklen_t length=sizeof error;if(getsockopt(op->fd,SOL_SOCKET,SO_ERROR,&error,&length))error=errno;
        break;
    }
    pthread_mutex_lock(&lock);if(s->closed||op->canceled)error=ECANCELED;
    s->connect_done=!error;s->connecting=NULL;
    uint32_t dos,status=socket_status(error,&dos);
    if(!tribes_completion_binding_post_io(s->completion,s->base,0,op->overlapped,status,dos)&&tribes_get_last_error()!=735){
        fputs("TribesPad: native connect completion could not be queued\n",stderr);abort();
    }
    close(op->fd);free(op);if(s->closed&&!s->reading&&!s->write_head)free_socket(s);--pending_connects;
    pthread_mutex_unlock(&lock);return NULL;
}
uint32_t tribes_socket_pending_connect_count(void){pthread_mutex_lock(&lock);uint32_t n=pending_connects;pthread_mutex_unlock(&lock);return n;}
/* Reached original contract: owned IPv4 nonblocking TCP, zero initial send,
   IOCP and no hEvent. Always starts a queued operation; even loopback native
   success is asynchronous and therefore must produce a completion packet. */
uint32_t tribes_connect_ex(uint64_t base,uint32_t handle,uint32_t address,uint32_t length,uint32_t send_buffer,uint32_t send_length,uint32_t sent,uint32_t overlapped){
    (void)sent; /* No send buffer: Windows ignores the byte-count pointer. */
    if(send_buffer&&send_length){fputs("TribesPad: ConnectEx initial send is not implemented\n",stderr);abort();}
    struct sockaddr_in destination;if(!sockaddr_in_guest(base,address,length,&destination))return 0;
    void *block=guest(base,overlapped,20);if(!block){tribes_set_last_error(10014);return 0;}
    uint32_t words[5];memcpy(words,block,20);
    if(words[4]){fputs("TribesPad: ConnectEx event delivery is not implemented\n",stderr);abort();}
    if(!destination.sin_addr.s_addr){tribes_set_last_error(10049);return 0;}
    pthread_mutex_lock(&lock);Context *c=find(base);Socket *s=sockets;
    if(!c||!c->references){pthread_mutex_unlock(&lock);tribes_set_last_error(10093);return 0;}
    while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){pthread_mutex_unlock(&lock);tribes_set_last_error(10038);return 0;}
    if(s->owner)ownership_stop();
    if(s->type!=1||s->family!=2){pthread_mutex_unlock(&lock);tribes_set_last_error(10045);return 0;}
    if(s->connecting){pthread_mutex_unlock(&lock);tribes_set_last_error(10037);return 0;}
    if(!s->completion){fputs("TribesPad: ConnectEx without a completion port is not implemented\n",stderr);abort();}
    struct sockaddr_in local,peer;socklen_t size=sizeof local;
    if(!getpeername(s->fd,(struct sockaddr *)&peer,&size)){pthread_mutex_unlock(&lock);tribes_set_last_error(10056);return 0;}
    size=sizeof local;
    if(getsockname(s->fd,(struct sockaddr *)&local,&size)||!local.sin_port){pthread_mutex_unlock(&lock);tribes_set_last_error(10022);return 0;}
    int flags=fcntl(s->fd,F_GETFL);
    if(flags<0){int error=errno;pthread_mutex_unlock(&lock);tribes_set_last_error(error_number(error));return 0;}
    if(!(flags&O_NONBLOCK)){fputs("TribesPad: blocking ConnectEx socket is not implemented\n",stderr);abort();}
    ConnectOperation *op=calloc(1,sizeof *op);int fd=op?fcntl(s->fd,F_DUPFD_CLOEXEC,3):-1;
    if(!op||fd<0){free(op);pthread_mutex_unlock(&lock);tribes_set_last_error(10055);return 0;}
    op->socket=s;op->fd=fd;op->address=destination;op->overlapped=overlapped;op->issuer=io_issuer();
    uint32_t pending[]={0x103,0};memcpy(block,pending,8);s->connecting=op;++pending_connects;
    pthread_attr_t attributes;int error=pthread_attr_init(&attributes);
    if(!error){error=pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED);if(!error){pthread_t thread;error=pthread_create(&thread,&attributes,run_connect,op);}pthread_attr_destroy(&attributes);}
    if(error){s->connecting=NULL;--pending_connects;close(fd);free(op);memcpy(block,words,8);pthread_mutex_unlock(&lock);tribes_set_last_error(10055);return 0;}
    pthread_mutex_unlock(&lock);tribes_set_last_error(997);return 0;
}
/* Original libuv waits with a zero-byte overlapped receive, then performs
   nonblocking synchronous reads. Stack WSABUF/flags/count pointers cannot be
   retained by a pending operation. Native I/O sees only private staging. */
struct SocketIo {Socket *socket;int fd,canceled;uint32_t overlapped,length;uint64_t issuer;unsigned char *data;SocketIo *next;};
static int detached(void *(*entry)(void *),void *operation){
    pthread_attr_t attr;int error=pthread_attr_init(&attr);
    if(!error){error=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);if(!error){pthread_t t;error=pthread_create(&t,&attr,entry,operation);}pthread_attr_destroy(&attr);}
    return error;
}
static void io_completion(SocketIo *op,uint32_t bytes,int error){
    uint32_t dos,status=socket_status(error,&dos);Socket *s=op->socket;
    if(!tribes_completion_binding_post_io(s->completion,s->base,bytes,op->overlapped,status,dos)&&tribes_get_last_error()!=735){
        fputs("TribesPad: native socket I/O completion could not be queued\n",stderr);abort();
    }
    close(op->fd);free(op->data);free(op);--pending_io;
}
static int io_wait(SocketIo *op,short events){
    for(;;){
        pthread_mutex_lock(&lock);int canceled=op->socket->closed||op->canceled;pthread_mutex_unlock(&lock);
        if(canceled)return ECANCELED;
        struct pollfd p={op->fd,events,0};int result=poll(&p,1,50);
        if(result<0){if(errno==EINTR)continue;return errno;}
        if(!result)continue;
        if(p.revents&POLLNVAL)return EBADF;
        if(p.revents&POLLERR){int error=0;socklen_t n=sizeof error;if(getsockopt(op->fd,SOL_SOCKET,SO_ERROR,&error,&n))return errno;if(error)return error;}
        if(p.revents&(events|POLLHUP))return 0;
    }
}
static void *run_zero_read(void *opaque){
    SocketIo *op=opaque;Socket *s=op->socket;int error=io_wait(op,POLLIN);
    pthread_mutex_lock(&lock);if(s->closed||op->canceled)error=ECANCELED;s->reading=NULL;
    io_completion(op,0,error);if(s->closed&&!s->connecting&&!s->write_head)free_socket(s);
    pthread_mutex_unlock(&lock);return NULL;
}
/* One writer per socket preserves submission order under backpressure. Each
   submitted request owns a captured descriptor and private payload until its
   actual send count/error has been published to the real completion queue. */
static void *run_writes(void *opaque){
    Socket *s=opaque;
    for(;;){
        pthread_mutex_lock(&lock);SocketIo *op=s->write_head;pthread_mutex_unlock(&lock);
        uint32_t sent=0;int error=0;
        while(sent<op->length){
            pthread_mutex_lock(&lock);int canceled=s->closed||op->canceled;pthread_mutex_unlock(&lock);
            if(canceled){error=ECANCELED;break;}
            ssize_t n=send(op->fd,op->data+sent,op->length-sent,0);
            if(n>0){sent+=(uint32_t)n;continue;}
            if(!n){error=EPIPE;break;}
            error=errno;if(error==EINTR){error=0;continue;}
            if(error==EAGAIN){error=io_wait(op,POLLOUT);if(!error)continue;}
            break;
        }
        pthread_mutex_lock(&lock);if(s->closed||op->canceled)error=ECANCELED;
        s->write_head=op->next;int more=s->write_head!=NULL;if(!more)s->write_tail=NULL;
        io_completion(op,sent,error);
        if(!more){if(s->closed&&!s->connecting&&!s->reading)free_socket(s);pthread_mutex_unlock(&lock);return NULL;}
        pthread_mutex_unlock(&lock);
    }
}
uint32_t tribes_socket_pending_io_count(void){pthread_mutex_lock(&lock);uint32_t n=pending_io;pthread_mutex_unlock(&lock);return n;}
/* Cancellation marks native operations; their workers publish the actual
   final status/count. The socket itself is neither shut down nor closed here.
   OVERLAPPED is an opaque request identity, not memory read by cancellation. */
static uint32_t cancel_socket_io(uint64_t base,uint32_t handle,uint32_t overlapped,int same_thread){
    if((handle&0xff000000u)!=0xb1000000u){fputs("TribesPad: cancellation for non-socket handles is not implemented\n",stderr);abort();}
    pthread_mutex_lock(&lock);Socket *s=sockets;while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}if(s->owner)ownership_stop();
    uint64_t issuer=same_thread?io_issuer():0;unsigned matches=0;
    ConnectOperation *connect=s->connecting;
    if(connect&&(!overlapped||connect->overlapped==overlapped)&&(!same_thread||connect->issuer==issuer)){connect->canceled=1;++matches;}
    SocketIo *read=s->reading;
    if(read&&(!overlapped||read->overlapped==overlapped)&&(!same_thread||read->issuer==issuer)){read->canceled=1;++matches;}
    for(SocketIo *write=s->write_head;write;write=write->next)
        if((!overlapped||write->overlapped==overlapped)&&(!same_thread||write->issuer==issuer)){write->canceled=1;++matches;}
    pthread_mutex_unlock(&lock);
    if(!matches&&!same_thread){tribes_set_last_error(1168);return 0;}
    return 1;
}
uint32_t tribes_cancel_socket_io_ex(uint64_t base,uint32_t socket,uint32_t overlapped){return cancel_socket_io(base,socket,overlapped,0);}
uint32_t tribes_cancel_socket_io(uint64_t base,uint32_t socket){return cancel_socket_io(base,socket,0,1);}
static int wsabufs(uint64_t base,uint32_t address,uint32_t count,uint32_t rows[64][2],uint32_t *length){
    if(!count||count>64)return 10022;
    void *p=guest(base,address,count*8);if(!p)return 10014;memcpy(rows,p,count*8);
    uint64_t total=0;
    for(uint32_t i=0;i<count;i++){total+=rows[i][0];if(total>INT32_MAX)return 10055;if(rows[i][0]&&!guest(base,rows[i][1],rows[i][0]))return 10014;}
    *length=(uint32_t)total;return 0;
}
static Socket *io_socket(uint64_t base,uint32_t handle){
    Context *c=find(base);if(!c||!c->references){fail(10093);return NULL;}
    Socket *s=sockets;while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){fail(10038);return NULL;}if(s->owner)ownership_stop();
    if(s->type!=1){fail(10045);return NULL;}
    int flags=fcntl(s->fd,F_GETFL);if(flags<0){fail(error_number(errno));return NULL;}
    if(!(flags&O_NONBLOCK)){fputs("TribesPad: blocking WSA socket I/O is not implemented\n",stderr);abort();}
    struct sockaddr_storage peer;socklen_t n=sizeof peer;
    if(getpeername(s->fd,(struct sockaddr *)&peer,&n)){fail(error_number(errno));return NULL;}
    return s;
}
static SocketIo *new_io(Socket *s,uint32_t overlapped){
    if(!s->completion){fputs("TribesPad: overlapped socket I/O without IOCP is not implemented\n",stderr);abort();}
    SocketIo *op=calloc(1,sizeof *op);int fd=op?fcntl(s->fd,F_DUPFD_CLOEXEC,3):-1;
    if(!op||fd<0){free(op);fail(10055);return NULL;}
    op->socket=s;op->fd=fd;op->overlapped=overlapped;op->issuer=io_issuer();return op;
}
static int io_header(uint64_t base,uint32_t overlapped,uint32_t old[5],void **header){
    *header=guest(base,overlapped,20);if(!*header)return 10014;memcpy(old,*header,20);
    if(old[4]){fputs("TribesPad: WSA socket I/O event delivery is not implemented\n",stderr);abort();}
    return 0;
}
uint32_t tribes_wsa_recv(uint64_t base,uint32_t handle,uint32_t buffers,uint32_t count,uint32_t bytes,uint32_t flags,uint32_t overlapped,uint32_t callback){
    if(callback){fputs("TribesPad: WSARecv completion callbacks are not implemented\n",stderr);abort();}
    void *flagp=guest(base,flags,4);uint32_t value;if(!flagp)return fail(10014);memcpy(&value,flagp,4);if(value)return fail(10045);
    uint32_t rows[64][2],length;int error=wsabufs(base,buffers,count,rows,&length);if(error)return fail(error);
    void *bytep=bytes?guest(base,bytes,4):NULL;if((bytes&&!bytep)||(!overlapped&&!bytep))return fail(10014);
    pthread_mutex_lock(&lock);Socket *s=io_socket(base,handle);if(!s){pthread_mutex_unlock(&lock);return UINT32_MAX;}
    if(overlapped){
        if(length){fputs("TribesPad: nonzero overlapped WSARecv is not implemented\n",stderr);abort();}
        if(s->reading){pthread_mutex_unlock(&lock);return fail(10035);}
        uint32_t old[5];void *header;error=io_header(base,overlapped,old,&header);if(error){pthread_mutex_unlock(&lock);return fail(error);}
        SocketIo *op=new_io(s,overlapped);if(!op){pthread_mutex_unlock(&lock);return UINT32_MAX;}
        uint32_t pending[]={0x103,0};memcpy(header,pending,8);s->reading=op;++pending_io;
        error=detached(run_zero_read,op);
        if(error){s->reading=NULL;--pending_io;close(op->fd);free(op);memcpy(header,old,8);pthread_mutex_unlock(&lock);return fail(10055);}
        pthread_mutex_unlock(&lock);return fail(997);
    }
    int fd=fcntl(s->fd,F_DUPFD_CLOEXEC,3);pthread_mutex_unlock(&lock);if(fd<0)return fail(10055);
    unsigned char *data=malloc(length?length:1);if(!data){close(fd);return fail(10055);}
    ssize_t n=recv(fd,data,length,0);error=errno;close(fd);
    if(n<0){free(data);return fail(error_number(error));}
    uint32_t offset=0,remaining=(uint32_t)n;
    for(uint32_t i=0;i<count&&remaining;i++){uint32_t used=rows[i][0]<remaining?rows[i][0]:remaining;if(used)memcpy(guest(base,rows[i][1],used),data+offset,used);offset+=used;remaining-=used;}
    free(data);value=(uint32_t)n;memcpy(bytep,&value,4);value=0;memcpy(flagp,&value,4);return 0;
}
uint32_t tribes_wsa_send(uint64_t base,uint32_t handle,uint32_t buffers,uint32_t count,uint32_t bytes,uint32_t flags,uint32_t overlapped,uint32_t callback){
    if(callback){fputs("TribesPad: WSASend completion callbacks are not implemented\n",stderr);abort();}
    if(flags)return fail(10045);
    uint32_t rows[64][2],length;int error=wsabufs(base,buffers,count,rows,&length);if(error)return fail(error);
    void *bytep=bytes?guest(base,bytes,4):NULL;if((bytes&&!bytep)||(!overlapped&&!bytep))return fail(10014);
    unsigned char *data=malloc(length?length:1);if(!data)return fail(10055);
    uint32_t offset=0;for(uint32_t i=0;i<count;i++){if(rows[i][0])memcpy(data+offset,guest(base,rows[i][1],rows[i][0]),rows[i][0]);offset+=rows[i][0];}
    pthread_mutex_lock(&lock);Socket *s=io_socket(base,handle);if(!s){pthread_mutex_unlock(&lock);free(data);return UINT32_MAX;}
    if(overlapped){
        uint32_t old[5];void *header;error=io_header(base,overlapped,old,&header);if(error){pthread_mutex_unlock(&lock);free(data);return fail(error);}
        SocketIo *op=new_io(s,overlapped);if(!op){pthread_mutex_unlock(&lock);free(data);return UINT32_MAX;}op->data=data;op->length=length;
        uint32_t pending[]={0x103,0};memcpy(header,pending,8);int first=!s->write_head;
        if(first)s->write_head=op;else s->write_tail->next=op;s->write_tail=op;++pending_io;
        error=first?detached(run_writes,s):0;
        if(error){s->write_head=s->write_tail=NULL;--pending_io;close(op->fd);free(data);free(op);memcpy(header,old,8);pthread_mutex_unlock(&lock);return fail(10055);}
        pthread_mutex_unlock(&lock);return fail(997);
    }
    if(s->write_head){pthread_mutex_unlock(&lock);free(data);return fail(10035);}
    int fd=fcntl(s->fd,F_DUPFD_CLOEXEC,3);pthread_mutex_unlock(&lock);if(fd<0){free(data);return fail(10055);}
    ssize_t n=send(fd,data,length,0);error=errno;close(fd);free(data);if(n<0)return fail(error_number(error));
    uint32_t actual=(uint32_t)n;memcpy(bytep,&actual,4);return 0;
}
/* Windows commits the connected socket context here; BSD already has its
   endpoint. Verify the actual connection before recording that commit. */
uint32_t tribes_setsockopt(uint64_t base,uint32_t handle,uint32_t level,uint32_t option,uint32_t value,uint32_t length){
    if((level==0xffff&&(option==4||option==0x20))||(level==41&&option==27)){
        int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;
        const void *p=guest(base,value,4);if(!p||length<4||length>INT32_MAX){close(fd);return fail(10014);}
        uint32_t raw;memcpy(&raw,p,4);int enabled=raw!=0;
        int native_level=level==41?IPPROTO_IPV6:SOL_SOCKET;
        int native_option=level==41?IPV6_V6ONLY:option==4?SO_REUSEADDR:SO_BROADCAST;
        int result=setsockopt(fd,native_level,native_option,&enabled,sizeof enabled),error=errno;close(fd);
        return result?fail(error_number(error)):0;
    }
    if(level!=0xffff||option!=0x7010||value||length){fprintf(stderr,"TribesPad: unsupported setsockopt level=0x%x option=0x%x\n",level,option);abort();}
    pthread_mutex_lock(&lock);Context *c=find(base);if(!c||!c->references){pthread_mutex_unlock(&lock);return fail(10093);}
    Socket *s=sockets;while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){pthread_mutex_unlock(&lock);return fail(10038);}
    struct sockaddr_storage peer;socklen_t size=sizeof peer;
    if(!s->connect_done||getpeername(s->fd,(struct sockaddr *)&peer,&size)){pthread_mutex_unlock(&lock);return fail(10057);}
    s->connect_context=1;pthread_mutex_unlock(&lock);return 0;
}
/* The native BSD provider has no layered providers: its registered socket
   identity is already the base handle. Return that guest identity, never fd.
   This only covers the reached synchronous SIO_BASE_HANDLE query. */
uint32_t tribes_wsa_ioctl(uint64_t base,uint32_t handle,uint32_t command,uint32_t input,uint32_t input_size,
                         uint32_t output,uint32_t capacity,uint32_t returned,uint32_t overlapped,uint32_t completion){
    pthread_mutex_lock(&lock);Context *c=find(base);
    if(!c||!c->references){pthread_mutex_unlock(&lock);return fail(10093);}
    Socket *s=sockets;while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s){pthread_mutex_unlock(&lock);return fail(10038);}
    if((command!=0x48000022&&command!=0xc8000006)||overlapped||completion){
        fprintf(stderr,"TribesPad: unsupported WSAIoctl contract code=0x%x overlapped=0x%x completion=0x%x\n",command,overlapped,completion);abort();
    }
    void *destination=guest(base,output,4),*count=guest(base,returned,4);
    if(capacity<4||!destination||!count){pthread_mutex_unlock(&lock);return fail(10014);}
    uint32_t result=handle;
    if(command==0xc8000006){
        static const unsigned char id[16]={0xb9,0x07,0xa2,0x25,0xf3,0xdd,0x60,0x46,0x8e,0xe9,0x76,0xe5,0x8c,0x74,0x06,0x3e};
        void *guid=guest(base,input,16);
        if(input_size!=16||!guid){pthread_mutex_unlock(&lock);return fail(10014);}
        unsigned char requested[16];memcpy(requested,guid,16);
        if(memcmp(requested,id,16)){pthread_mutex_unlock(&lock);return fail(10045);}
        result=TRIBES_CONNECT_EX_ENTRY;
    }
    uint32_t bytes=4;memcpy(destination,&result,4);memcpy(count,&bytes,4);
    pthread_mutex_unlock(&lock);return 0;
}
/* Winsock fd_set is a count plus 32-bit socket identities, not a BSD bitset.
   Poll only private duplicates and publish compact ready subsets afterwards.
   The current original slow-poll worker uses one socket and a180s timeout. */
uint32_t tribes_socket_select(uint64_t base,uint32_t nfds,uint32_t read_set,uint32_t write_set,uint32_t except_set,uint32_t timeout){
    (void)nfds;uint32_t addresses[]={read_set,write_set,except_set},counts[3]={0},sets[3][64],handles[192];
    struct pollfd native[192];unsigned used=0;uint32_t error=0,result=0;int milliseconds=-1;
    pthread_mutex_lock(&lock);Context *c=find(base);int initialized=c&&c->references;pthread_mutex_unlock(&lock);
    if(!initialized)return fail(10093);
    if(timeout){
        int32_t value[2];void *p=guest(base,timeout,8);if(!p)return fail(10014);memcpy(value,p,8);
        if(value[0]<0||value[1]<0||value[1]>=1000000)return fail(10022);
        uint64_t ms=(uint64_t)value[0]*1000+((uint32_t)value[1]+999)/1000;
        if(ms>INT_MAX){fputs("TribesPad: unsupported select timeout range\n",stderr);abort();}
        milliseconds=(int)ms;
    }
    for(unsigned group=0;group<3;group++)if(addresses[group]){
        void *p=guest(base,addresses[group],4);if(!p){error=10014;goto done;}memcpy(&counts[group],p,4);
        if(counts[group]>64){fputs("TribesPad: unsupported select fd_set capacity\n",stderr);abort();}
        p=guest(base,addresses[group],4+counts[group]*4);if(!p){error=10014;goto done;}
        if(counts[group])memcpy(sets[group],(char *)p+4,counts[group]*4);
        for(unsigned j=0;j<counts[group];j++){
            unsigned slot=0;while(slot<used&&handles[slot]!=sets[group][j])slot++;
            if(slot==used){int fd=borrow(base,sets[group][j]);if(fd<0){error=tribes_get_last_error();goto done;}handles[used]=sets[group][j];native[used++]=(struct pollfd){fd,0,0};}
            native[slot].events|=group==0?POLLIN:group==1?POLLOUT:POLLPRI;
        }
    }
    if(!used){error=10022;goto done;}
    if(poll(native,used,milliseconds)<0){error=error_number(errno);goto done;}
    for(unsigned i=0;i<used;i++)if(native[i].revents&POLLNVAL){error=10038;goto done;}
    for(unsigned group=0;group<3;group++)if(addresses[group]){
        uint32_t ready[64],count=0;
        for(unsigned j=0;j<counts[group];j++){
            unsigned slot=0;while(slot<used&&handles[slot]!=sets[group][j])slot++;if(slot==used)abort();
            short events=native[slot].revents;
            int yes=group==0?(events&(POLLIN|POLLHUP|POLLERR)):group==1?((events&POLLOUT)&&!(events&POLLERR)):(events&(POLLPRI|POLLERR));
            if(yes)ready[count++]=sets[group][j];
        }
        void *p=guest(base,addresses[group],4+count*4);
        memcpy(p,&count,4);if(count)memcpy((char *)p+4,ready,count*4);result+=count;
    }
 done:
    for(unsigned i=0;i<used;i++)close(native[i].fd);
    return error?fail(error):result;
}
uint32_t tribes_ioctlsocket(uint64_t base,uint32_t handle,uint32_t command,uint32_t pointer) {
    int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;void *p=guest(base,pointer,4);if(!p){close(fd);return fail(10014);}uint32_t value;memcpy(&value,p,4);int result;
    if(command==0x8004667e){int flags=fcntl(fd,F_GETFL);result=flags<0?-1:fcntl(fd,F_SETFL,value?flags|O_NONBLOCK:flags&~O_NONBLOCK);}
    else if(command==0x4004667f){int available=0;result=ioctl(fd,FIONREAD,&available);if(!result){value=(uint32_t)available;memcpy(p,&value,4);}}
    else {close(fd);return fail(10022);}int error=errno;close(fd);return result?fail(error_number(error)):0;
}
uint32_t tribes_sendto(uint64_t base,uint32_t handle,uint32_t pointer,uint32_t length,uint32_t flags,uint32_t destination,uint32_t address_length) {
    int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;if(length>INT32_MAX||flags&~4u){close(fd);return fail(10022);}void *p=guest(base,pointer,length);if(!p){close(fd);return fail(10014);}struct sockaddr_in address;if(!sockaddr_in_guest(base,destination,address_length,&address)){close(fd);return UINT32_MAX;}
    ssize_t result=sendto(fd,p,length,flags?MSG_DONTROUTE:0,(struct sockaddr *)&address,sizeof address);int error=errno;close(fd);return result<0?fail(error_number(error)):(uint32_t)result;
}
uint32_t tribes_recvfrom(uint64_t base,uint32_t handle,uint32_t pointer,uint32_t length,uint32_t flags,uint32_t source,uint32_t source_length) {
    int fd=borrow(base,handle);if(fd<0)return UINT32_MAX;if(length>INT32_MAX||flags&~2u){close(fd);return fail(10022);}void *p=guest(base,pointer,length),*out=source?guest(base,source,16):NULL,*nptr=source_length?guest(base,source_length,4):NULL;uint32_t capacity=0;if(nptr)memcpy(&capacity,nptr,4);
    if(!p||!!source!=!!source_length||(source&&(!out||!nptr||capacity<16))){close(fd);return fail(10014);}struct sockaddr_in address;socklen_t n=sizeof address;
    /* MSG_TRUNC reports an oversized datagram; Windows returns WSAEMSGSIZE. */
    struct iovec vector={p,length};struct msghdr message;memset(&message,0,sizeof message);memset(&address,0,sizeof address);message.msg_name=&address;message.msg_namelen=n;message.msg_iov=&vector;message.msg_iovlen=1;
    ssize_t result=recvmsg(fd,&message,flags?MSG_PEEK:0);int error=errno;close(fd);
    if(result<0)return fail(error_number(error));if(source){write_address(out,&address);capacity=16;memcpy(nptr,&capacity,4);}if(message.msg_flags&MSG_TRUNC)return fail(10040);return (uint32_t)result;
}
