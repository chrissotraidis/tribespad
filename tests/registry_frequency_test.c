#include "../port/core/guest_registry.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Inject OS responses at the actual registry service boundary. */
static struct { int error; size_t size; uint64_t hz; uint32_t result,mhz; } cases[]={
 {ENOENT,8,0,0,2400},{0,0,0,0,2400},{0,8,0,0,2400},
 {0,8,3200999999,0,3200},{EIO,0,0,31,0},{EPERM,8,0,31,0},
 {0,4,0,13,0},{0,4,3200000000,13,0},{0,16,0,13,0},
 {0,8,999999,13,0},{0,8,UINT64_MAX,13,0}
};
static unsigned current;
int fixture_sysctlbyname(const char *name,void *out,size_t *size,void *newp,size_t newsize){
 assert(!strcmp(name,"hw.cpufrequency")&&out&&*size==8&&!newp&&!newsize);
 memcpy(out,&cases[current].hz,8);*size=cases[current].size;
 errno=cases[current].error;return errno?-1:0;
}
static unsigned char memory[8192];
static void wide(unsigned at,const char *s){do{uint16_t c=(unsigned char)*s;memcpy(memory+at,&c,2);at+=2;}while(*s++);}
static uint32_t read32(unsigned at){uint32_t v;memcpy(&v,memory+at,4);return v;}
int main(void){
 uint64_t base=(uintptr_t)memory;
 wide(128,"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
 wide(512,"~MHz");assert(!tribes_reg_open_key_ex_w(base,0x80000002,128,0,1,32));
 uint32_t key=read32(32);
 for(current=0;current<sizeof cases/sizeof *cases;current++){
  uint32_t size=4;memset(memory+1024,0xa5,16);memcpy(memory+1032,&size,4);
  uint32_t r=tribes_reg_query_value_ex_w(base,key,512,0,1024,1028,1032);
  assert(r==cases[current].result);
  if(!r)assert(read32(1024)==4&&read32(1028)==cases[current].mhz&&read32(1032)==4);
  else assert(read32(1024)==0xa5a5a5a5&&read32(1028)==0xa5a5a5a5&&read32(1032)==4);
 }
 assert(!tribes_reg_close_key(base,key));
 puts("PASS: 11 injected CPU-frequency results: native value, declared fallback, OS errors, malformed sizes and numeric bounds");
}
