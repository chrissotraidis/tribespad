#include "server_query.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static uint32_t le32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static double now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
int tb_parse_ping(const uint8_t*p,size_t n,uint32_t nonce,TBServerPing*out){
 if(!p||!out||n<7||p[0]!=16||le32(p+2)!=nonce)return 0;
 size_t label=p[6];if(n<7+label)return 0;
 uint32_t version=label==4&&!memcmp(p+7,"VER3",4)?3:label==4&&!memcmp(p+7,"VER5",4)?5:0;
 if(!version)return 0;
 size_t pos=7+label,fields=version==5?12:8;if(n<pos+fields+1)return 0;
 TBServerPing r={0};r.queryVersion=version;
 if(version==5){r.protocol=le32(p+pos);pos+=4;}
 r.minProtocol=le32(p+pos);pos+=4;r.build=le32(p+pos);pos+=4;
 size_t len=p[pos++];if(n-pos<len)return 0;
 memcpy(r.name,p+pos,len);r.name[len]=0;*out=r;return 1;
}
int tb_server_ping(const char*host,uint16_t port,unsigned timeout,TBServerPing*out,char*error,size_t capacity){
 if(capacity&&!error)return 0;if(capacity)error[0]=0;
 if(!host||!out||!port||!timeout||timeout>30000){snprintf(error,capacity,"Invalid endpoint or timeout");return 0;}
 struct sockaddr_in remote={.sin_family=AF_INET,.sin_port=htons(port)};
 if(inet_pton(AF_INET,host,&remote.sin_addr)!=1){snprintf(error,capacity,"An IPv4 address is required");return 0;}
 int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0){snprintf(error,capacity,"socket: %s",strerror(errno));return 0;}
 if(connect(fd,(struct sockaddr*)&remote,sizeof(remote))<0){snprintf(error,capacity,"connect: %s",strerror(errno));close(fd);return 0;}
 uint32_t nonce=arc4random();uint8_t packet[6]={14,2,nonce&255,(nonce>>8)&255,(nonce>>16)&255,(nonce>>24)&255};
 double start=now_ms(),deadline=start+timeout;int success=0;
 if(send(fd,packet,sizeof(packet),0)!=sizeof(packet)){snprintf(error,capacity,"send: %s",strerror(errno));close(fd);return 0;}
 while(now_ms()<deadline){
  struct pollfd p={fd,POLLIN,0};int remaining=(int)(deadline-now_ms());if(remaining<1)break;
  int ready=poll(&p,1,remaining);if(ready<0){if(errno==EINTR)continue;snprintf(error,capacity,"poll: %s",strerror(errno));break;}
  if(!ready)break;
  uint8_t reply[2048];ssize_t n=recv(fd,reply,sizeof(reply),0);
  if(n<0){snprintf(error,capacity,"recv: %s",strerror(errno));break;}
  if(tb_parse_ping(reply,(size_t)n,nonce,out)){out->pingMS=(float)(now_ms()-start);success=1;break;}
 }
 if(!success&&(!capacity||!error[0]))snprintf(error,capacity,"No valid Tribes 2 ping response before timeout");
 close(fd);return success;
}
