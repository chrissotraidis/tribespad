/* Who is playing Starsiege: Tribes right now, for the game picker. The
 * community masters (t1m1-3.tribes1.co and t1m1-3.pu.net, port 28000) list
 * servers; each server answers the game's own ping with its player count and
 * name. The wire format follows the masters' maintainer's library, t1net-go
 * (github.com/TheKigen/t1net-go, master.go and tribes.go, Apache-2.0):
 *   query, to a master or a server: 10 03 FF 00 key(2, LE) 00 00
 *   master reply: 10 06 n total key(2) 00 66 name(pascal) motd(pascal)
 *                 count(2, BE) then count x [06 ip(4) port(2, LE)], n of total
 *   server reply: 10 04 ?? F0 key(2) max players version(2, LE) name(zero-terminated)
 * Read-only UDP, the same questions the game's own browser asks. */
#include "t1_live.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static const char *const masters[]={"t1m1.tribes1.co","t1m2.tribes1.co","t1m3.tribes1.co","t1m1.pu.net","t1m2.pu.net","t1m3.pu.net"};
typedef struct {struct sockaddr_in address;int answered,players;char name[64];} Server;

static long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000L;}
static int same(const struct sockaddr_in *a,const struct sockaddr_in *b){return a->sin_addr.s_addr==b->sin_addr.s_addr&&a->sin_port==b->sin_port;}

int tb_t1_live(TBT1Live *out,int timeout_ms){
    memset(out,0,sizeof *out);
    int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return 0;
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
    uint16_t key=(uint16_t)arc4random();
    uint8_t query[8]={0x10,0x03,0xff,0x00,(uint8_t)key,(uint8_t)(key>>8),0,0};
    /* Ask every master; their lists overlap, so servers are merged. */
    struct sockaddr_in master_addresses[6];int master_count=0;
    for(unsigned i=0;i<sizeof masters/sizeof *masters;i++){
        struct addrinfo hints={0},*found=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
        if(getaddrinfo(masters[i],"28000",&hints,&found)||!found)continue;
        struct sockaddr_in a=*(struct sockaddr_in *)found->ai_addr;freeaddrinfo(found);
        int duplicate=0;for(int j=0;j<master_count;j++)if(same(&a,&master_addresses[j]))duplicate=1;
        if(duplicate)continue;master_addresses[master_count++]=a;
        sendto(fd,query,sizeof query,0,(struct sockaddr *)&a,sizeof a);
    }
    Server *servers=calloc(1000,sizeof *servers);int count=0;if(!servers){close(fd);return 0;}
    uint8_t d[65536];long master_end=now_ms()+timeout_ms/2,end=now_ms()+timeout_ms;int pinged=0;
    while(now_ms()<end){
        /* Once the masters have had their time, ping every listed server once. */
        if(!pinged&&now_ms()>=master_end){pinged=1;
            for(int i=0;i<count;i++)sendto(fd,query,sizeof query,0,(struct sockaddr *)&servers[i].address,sizeof servers[i].address);
            if(!count)break;}
        struct pollfd p={fd,POLLIN,0};long left=(pinged?end:master_end)-now_ms();
        if(poll(&p,1,(int)(left>0?left:1))<=0)continue;
        struct sockaddr_in from;socklen_t length=sizeof from;
        ssize_t n=recvfrom(fd,d,sizeof d,0,(struct sockaddr *)&from,&length);
        if(n<8||d[0]!=0x10||d[4]!=(uint8_t)key||d[5]!=(uint8_t)(key>>8))continue;
        if(d[1]==0x06&&d[6]==0x00&&d[7]==0x66){
            out->masters_answered++;size_t i=8;
            for(int s=0;s<2;s++){if(i>=(size_t)n)break;i+=1+d[i];}  /* name, motd */
            if(i+2>(size_t)n)continue;unsigned listed=(unsigned)d[i]<<8|d[i+1];i+=2;
            for(unsigned k=0;k<listed&&i+7<=(size_t)n&&count<1000;k++,i+=7){
                if(d[i]!=6)break;struct sockaddr_in a={0};a.sin_len=sizeof a;a.sin_family=AF_INET;
                memcpy(&a.sin_addr,d+i+1,4);a.sin_port=htons((uint16_t)(d[i+5]|d[i+6]<<8));
                int duplicate=0;for(int j=0;j<count;j++)if(same(&a,&servers[j].address))duplicate=1;
                if(!duplicate)servers[count++].address=a;
            }
        } else if(d[1]==0x04&&n>=10&&d[3]==0xf0){
            for(int i=0;i<count;i++)if(same(&from,&servers[i].address)&&!servers[i].answered){
                servers[i].answered=1;servers[i].players=d[7];
                size_t len=0;while(10+len<(size_t)n&&d[10+len]&&len<sizeof servers[i].name-1)len++;
                memcpy(servers[i].name,d+10,len);servers[i].name[len]=0;break;
            }
        }
    }
    close(fd);
    out->servers_listed=count;
    for(int i=0;i<count;i++)if(servers[i].answered){
        out->servers_answered++;
        if(servers[i].players>0){out->people+=servers[i].players;out->busy_servers++;
            if(servers[i].players>out->top_players){out->top_players=servers[i].players;strlcpy(out->top_name,servers[i].name,sizeof out->top_name);}}
    }
    free(servers);
    return out->masters_answered>0;
}
