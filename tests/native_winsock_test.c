#include "../port/core/native_winsock.h"
#include "../port/core/guest_heap.h"
#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
static unsigned char memory[8][4096];static uint64_t bases[8];
static uint16_t word16(void *p){uint16_t v;memcpy(&v,p,2);return v;}
static void put32(unsigned i,uint32_t address,uint32_t value){memcpy(memory[i]+address,&value,4);}
static void address(unsigned i,unsigned offset,uint16_t port){memset(memory[i]+offset,0,16);memory[i][offset]=2;uint16_t network=(uint16_t)tribes_network_u16(port);uint32_t loop=tribes_network_u32(INADDR_LOOPBACK);memcpy(memory[i]+offset+2,&network,2);memcpy(memory[i]+offset+4,&loop,4);}
static void ready(uint64_t base,uint32_t s){uint32_t set[]={1,s},timer[]={1,0};memcpy((void *)(uintptr_t)(base+1801),set,8);memcpy((void *)(uintptr_t)(base+1821),timer,8);assert(tribes_socket_select(base,0,1801,0,0,1821)==1);uint32_t result[2];memcpy(result,(void *)(uintptr_t)(base+1801),8);assert(result[0]==1&&result[1]==s);}
static void *worker(void *arg){unsigned i=(unsigned)(uintptr_t)arg;for(unsigned n=0;n<1000;n++){assert(!tribes_wsa_startup(bases[i],0x101,17));uint32_t s=tribes_socket(bases[i],n&1?23:2,2,0);assert(s!=UINT32_MAX);put32(i,1001,628);assert(!tribes_getsockopt(bases[i],s,0xffff,0x2005,1025,1001));uint32_t flags,protocol;memcpy(&flags,memory[i]+1025,4);memcpy(&protocol,memory[i]+1025+92,4);assert(flags==9&&protocol==17);uint32_t returned;assert(!tribes_wsa_ioctl(bases[i],s,0x48000022,0,0,1701,4,1709,0,0));memcpy(&returned,memory[i]+1701,4);assert(returned==s);assert(!tribes_closesocket(bases[i],s));assert(!tribes_wsa_cleanup(bases[i]));}return NULL;}
int main(void){
    tribes_set_last_error(12345);
    for(uint32_t i=0;i<65536;i++)assert(tribes_network_u16(i|0xa5a50000)==htons((uint16_t)i)&&tribes_network_u16(tribes_network_u16(i))==i);
    uint32_t random=1;for(unsigned i=0;i<100000;i++){random=random*1664525u+1013904223u;assert(tribes_network_u32(random)==htonl(random)&&tribes_network_u32(tribes_network_u32(random))==random);}
    assert(tribes_get_last_error()==12345);
    for(unsigned i=0;i<8;i++)bases[i]=(uintptr_t)memory[i];uint64_t base=bases[0];assert(tribes_socket(base,2,2,0)==UINT32_MAX&&tribes_get_last_error()==10093);
    memset(memory[0],0xa5,sizeof memory[0]);tribes_set_last_error(123);assert(tribes_wsa_startup(base,1,17)==10092&&memory[0][17]==0xa5&&tribes_get_last_error()==123);assert(tribes_wsa_startup(base,0x101,0)==10014);
    assert(!tribes_wsa_startup(base,0x101,17)&&tribes_get_last_error()==123);assert(word16(memory[0]+17)==0x101&&word16(memory[0]+19)==0x101&&memory[0][16]==0xa5&&memory[0][417]==0xa5);struct rlimit limits;assert(!getrlimit(RLIMIT_NOFILE,&limits));assert(word16(memory[0]+407)==(limits.rlim_cur>65535?65535:limits.rlim_cur)&&word16(memory[0]+409)==65507);
    assert(!tribes_wsa_startup(base,0x202,17));assert(!tribes_wsa_cleanup(base));uint32_t s=tribes_socket(base,2,2,17);assert(s!=UINT32_MAX);uint32_t original=s;assert(!tribes_wsa_ioctl(base,s,0x48000022,0,0,701,4,705,0,0));memcpy(&s,memory[0]+701,4);assert(s==original);assert(tribes_socket(base,24,2,0)==UINT32_MAX&&tribes_get_last_error()==10047);
    address(0,512,0);assert(!tribes_bind_socket(base,s,512,16));put32(0,601,16);memset(memory[0]+620,0xa5,20);assert(!tribes_getsockname(base,s,621,601));assert(memory[0][620]==0xa5&&memory[0][637]==0xa5);uint16_t port=ntohs(word16(memory[0]+623));assert(port);
    put32(0,701,1);assert(!tribes_ioctlsocket(base,s,0x8004667e,701));assert(tribes_recvfrom(base,s,1024,64,0,0,0)==UINT32_MAX&&tribes_get_last_error()==10035);
    int peer=socket(AF_INET,SOCK_DGRAM,0);assert(peer>=0);struct sockaddr_in local={0};local.sin_len=sizeof local;local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(!bind(peer,(struct sockaddr *)&local,sizeof local));socklen_t n=sizeof local;assert(!getsockname(peer,(struct sockaddr *)&local,&n));struct timeval timeout={1,0};assert(!setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout));
    address(0,800,ntohs(local.sin_port));memcpy(memory[0]+1024,"guest UDP payload",17);assert(tribes_sendto(base,s,1024,17,0,800,16)==17);char payload[64];struct sockaddr_in remote;n=sizeof remote;assert(recvfrom(peer,payload,sizeof payload,0,(struct sockaddr *)&remote,&n)==17&&!memcmp(payload,"guest UDP payload",17));assert(ntohs(remote.sin_port)==port);
    assert(sendto(peer,"native reply",12,0,(struct sockaddr *)&remote,sizeof remote)==12);ready(base,s);put32(0,601,16);assert(tribes_recvfrom(base,s,1024,64,2,621,601)==12&&!memcmp(memory[0]+1024,"native reply",12));assert(tribes_recvfrom(base,s,1024,64,0,621,601)==12);assert(ntohs(word16(memory[0]+623))==ntohs(local.sin_port));
    assert(sendto(peer,"oversized",9,0,(struct sockaddr *)&remote,sizeof remote)==9);ready(base,s);assert(tribes_recvfrom(base,s,1024,2,0,0,0)==UINT32_MAX&&tribes_get_last_error()==10040&&!memcmp(memory[0]+1024,"ov",2));assert(tribes_recvfrom(base,s,1024,64,0,0,0)==UINT32_MAX&&tribes_get_last_error()==10035);
    assert(!tribes_sendto(base,s,0,0,0,800,16));assert(recv(peer,payload,sizeof payload,0)==0);
    assert(!tribes_wsa_cleanup(base));assert(tribes_closesocket(base,s)==UINT32_MAX&&tribes_get_last_error()==10093);assert(!tribes_wsa_startup(base,0x101,17));assert(tribes_closesocket(base,s)==UINT32_MAX&&tribes_get_last_error()==10038);assert(!tribes_wsa_cleanup(base));assert(!close(peer));
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: all 65536 short and 100000 long network-order conversions, LastError preservation, real bidirectional native/guest UDP using converted ports/addresses, nonblocking/peek/truncation/zero datagrams, startup negotiation/refcounts, cleanup and 8000 concurrent socket lifetimes with protocol queries");return 0;
}
