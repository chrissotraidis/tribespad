#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {uint32_t queryVersion,protocol,minProtocol,build;char name[256];float pingMS;} TBServerPing;
int tb_parse_ping(const uint8_t*,size_t,uint32_t,TBServerPing*);
int tb_server_ping(const char*,uint16_t,unsigned,TBServerPing*,char*,size_t);
