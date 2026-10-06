/* Darwin interface snapshot -> Windows x86 IP_ADAPTER_ADDRESSES.
 * The reached libuv query skips anycast, multicast and DNS server lists.
 * Names are Darwin interface names; metadata unavailable from getifaddrs is
 * zero. This does not expose Windows DHCP, route metrics, LUIDs or DNS config.
 * Host-owned snapshot and output staging prevent nested guest pointers from
 * crossing the OS boundary. Only the final checked copy touches guest output.
 */
#include "guest_adapters.h"
#include <errno.h>
#include <stddef.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ADAPTER_BYTES=376, UNICAST_BYTES=48 };
static int selected(const struct ifaddrs *p,uint32_t family){
 return p->ifa_addr&&((p->ifa_addr->sa_family==AF_INET&&(family==0||family==2))||(p->ifa_addr->sa_family==AF_INET6&&(family==0||family==23)));
}
static uint64_t align8(uint64_t n){return (n+7)&~UINT64_C(7);}
static void put32(unsigned char *p,uint32_t v){memcpy(p,&v,4);}
static unsigned prefix(const struct ifaddrs *p){
 if(!p->ifa_netmask)return 255; /* documented unknown prefix */
 const unsigned char *bytes;unsigned count;
 if(p->ifa_addr->sa_family==AF_INET){bytes=(const unsigned char *)&((const struct sockaddr_in *)p->ifa_netmask)->sin_addr;count=4;}
 else{bytes=(const unsigned char *)&((const struct sockaddr_in6 *)p->ifa_netmask)->sin6_addr;count=16;}
 unsigned result=0;int zero=0;
 for(unsigned i=0;i<count;i++)for(unsigned bit=128;bit;bit>>=1){if(bytes[i]&bit){if(zero)return 255;result++;}else zero=1;}
 return result;
}
static int first(const struct ifaddrs *head,const struct ifaddrs *p,uint32_t family){
 for(const struct ifaddrs *q=head;q!=p;q=q->ifa_next)if(selected(q,family)&&!strcmp(q->ifa_name,p->ifa_name))return 0;
 return 1;
}
static int range(uint64_t base,uint32_t address,uint64_t length){
 return base&&address&&(uint64_t)address+length<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-((uint64_t)address+length-1);
}
uint32_t tribes_get_adapters_addresses(uint64_t base,uint32_t family,uint32_t flags,uint32_t reserved,uint32_t output,uint32_t size_pointer){
 if((family!=0&&family!=2&&family!=23)||reserved||!range(base,size_pointer,4))return 87;
 /* Keep unreached DNS/anycast/multicast/gateway requests explicit. */
 if(flags!=14){fputs("TribesPad: unsupported GetAdaptersAddresses flags\n",stderr);abort();}
 uint32_t capacity;memcpy(&capacity,(void *)(uintptr_t)(base+size_pointer),4);
 struct ifaddrs *head=NULL;
 if(getifaddrs(&head)){return errno==ENOMEM?8:31;}
 uint64_t required=0;
 for(const struct ifaddrs *p=head;p;p=p->ifa_next)if(selected(p,family)){
  if(first(head,p,family))required+=align8(ADAPTER_BYTES+5*(strlen(p->ifa_name)+1)+2);
  required+=align8(UNICAST_BYTES+(p->ifa_addr->sa_family==AF_INET?16:28));
 }
 if(!required){freeifaddrs(head);return 232;}
 if(required>UINT32_MAX){freeifaddrs(head);return 87;}
 if(!output||capacity<required){uint32_t size=(uint32_t)required;freeifaddrs(head);memcpy((void *)(uintptr_t)(base+size_pointer),&size,4);return 111;}
 if(!range(base,output,required)){freeifaddrs(head);return 87;}
 unsigned char *buffer=calloc(1,(size_t)required);if(!buffer){freeifaddrs(head);return 8;}
 uint32_t cursor=0,previous=0;int have_previous=0;
 for(const struct ifaddrs *p=head;p;p=p->ifa_next)if(selected(p,family)&&first(head,p,family)){
  uint32_t at=cursor;unsigned char *adapter=buffer+at;size_t name_length=strlen(p->ifa_name)+1;
  cursor+=(uint32_t)align8(ADAPTER_BYTES+5*name_length+2);
  if(have_previous)put32(buffer+previous+8,output+at);previous=at;have_previous=1;
  put32(adapter,ADAPTER_BYTES);
  /* AF_LINK supplies the snapshot's index and hardware metadata. */
  for(const struct ifaddrs *q=head;q;q=q->ifa_next)if(q->ifa_addr&&q->ifa_addr->sa_family==AF_LINK&&!strcmp(q->ifa_name,p->ifa_name)){
   const struct sockaddr_dl *dl=(const struct sockaddr_dl *)q->ifa_addr;
   put32(adapter+4,dl->sdl_index);put32(adapter+72,dl->sdl_index);
   if(dl->sdl_alen<=8&&(size_t)dl->sdl_len>=offsetof(struct sockaddr_dl,sdl_data)+dl->sdl_nlen+dl->sdl_alen){memcpy(adapter+44,LLADDR(dl),dl->sdl_alen);put32(adapter+52,dl->sdl_alen);}
   if(q->ifa_data){const struct if_data *data=q->ifa_data;put32(adapter+60,data->ifi_mtu);}
   /* Darwin IFT types use the same IANA assignments for these interfaces. */
   put32(adapter+64,dl->sdl_type);break;
  }
  if(p->ifa_flags&IFF_LOOPBACK)put32(adapter+64,24);
  put32(adapter+68,(p->ifa_flags&IFF_UP)?((p->ifa_flags&IFF_RUNNING)?1:5):2);
  uint32_t adapter_flags=(p->ifa_flags&IFF_MULTICAST)?0:16;
  for(const struct ifaddrs *q=head;q;q=q->ifa_next)if(q->ifa_addr&&!strcmp(q->ifa_name,p->ifa_name)){if(q->ifa_addr->sa_family==AF_INET)adapter_flags|=128;if(q->ifa_addr->sa_family==AF_INET6)adapter_flags|=256;}
  put32(adapter+56,adapter_flags);
  uint32_t name=at+ADAPTER_BYTES,description=name+(uint32_t)name_length,friendly=description+2*(uint32_t)name_length,suffix=friendly+2*(uint32_t)name_length;
  put32(adapter+12,output+name);memcpy(buffer+name,p->ifa_name,name_length);
  put32(adapter+32,output+suffix);put32(adapter+36,output+description);put32(adapter+40,output+friendly);
  for(size_t i=0;i<name_length;i++){buffer[description+2*i]=(unsigned char)p->ifa_name[i];buffer[friendly+2*i]=(unsigned char)p->ifa_name[i];}
  uint32_t previous_unicast=0;
  for(const struct ifaddrs *q=head;q;q=q->ifa_next)if(selected(q,family)&&!strcmp(q->ifa_name,p->ifa_name)){
   uint32_t ua=cursor,sa=ua+UNICAST_BYTES;unsigned size=q->ifa_addr->sa_family==AF_INET?16:28;cursor+=(uint32_t)align8(UNICAST_BYTES+size);
   if(previous_unicast)put32(buffer+previous_unicast+8,output+ua);else put32(adapter+16,output+ua);previous_unicast=ua;
   put32(buffer+ua,UNICAST_BYTES);put32(buffer+ua+12,output+sa);put32(buffer+ua+16,size);buffer[ua+44]=(unsigned char)prefix(q);
   /* Prefix/suffix origins, DAD and lease fields are unavailable in this
      snapshot API and remain unspecified (zero); consumer reads none. */
   if(size==16){const struct sockaddr_in *in=(const struct sockaddr_in *)q->ifa_addr;buffer[sa]=2;memcpy(buffer+sa+2,&in->sin_port,2);memcpy(buffer+sa+4,&in->sin_addr,4);}
   else{const struct sockaddr_in6 *in=(const struct sockaddr_in6 *)q->ifa_addr;buffer[sa]=23;memcpy(buffer+sa+2,&in->sin6_port,2);memcpy(buffer+sa+4,&in->sin6_flowinfo,4);memcpy(buffer+sa+8,&in->sin6_addr,16);put32(buffer+sa+24,in->sin6_scope_id);}
  }
 }
 freeifaddrs(head);
 if(cursor!=required){fputs("TribesPad: adapter snapshot size mismatch\n",stderr);abort();}
 memcpy((void *)(uintptr_t)(base+output),buffer,(size_t)required);free(buffer);return 0;
}
