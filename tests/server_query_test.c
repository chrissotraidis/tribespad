#include "server_query.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char**argv){
 if(argc==3){TBServerPing r={0};char error[256]={0};unsigned port=0;if(sscanf(argv[2],"%u",&port)!=1||port>65535)return 2;if(!tb_server_ping(argv[1],port,3000,&r,error,sizeof(error))){fprintf(stderr,"FAIL: %s\n",error);return 1;}printf("name=%s query=%u protocol=%u min=%u build=%u ping=%.1fms\n",r.name,r.queryVersion,r.protocol,r.minProtocol,r.build,r.pingMS);return 0;}
 uint8_t p[]={16,2,1,2,3,4,4,'V','E','R','5',52,0,0,0,34,0,0,0,202,97,0,0,4,'T','e','s','t'};TBServerPing r={0};assert(tb_parse_ping(p,sizeof(p),0x04030201,&r));assert(r.queryVersion==5&&r.protocol==52&&r.minProtocol==34&&r.build==25034&&!strcmp(r.name,"Test"));
 for(size_t n=0;n<sizeof(p);n++)assert(!tb_parse_ping(p,n,0x04030201,&r));assert(!tb_parse_ping(p,sizeof(p),0x04030202,&r));p[6]=255;assert(!tb_parse_ping(p,sizeof(p),0x04030201,&r));puts("PASS: ping parser, nonce validation, and every truncated packet rejected");return 0;
}
