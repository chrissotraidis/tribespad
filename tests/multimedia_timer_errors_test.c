#include "../port/core/multimedia_timer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static unsigned scenario;
int timer_fake_resolution(clockid_t clock,struct timespec *t){
 assert(clock==CLOCK_MONOTONIC);
 static const struct timespec values[]={{0,1},{0,1000000},{0,1000001},{65,535000000},{65,535000001},{66,0},{0,0},{-1,0},{0,-1},{0,1000000000}};
 if(scenario==10)return -1;*t=values[scenario];return 0;
}
int main(void){
 uint8_t memory[32];uint64_t base=(uintptr_t)memory;
 uint32_t expected[]={1,1,2,65535,0,0,0,0,0,0,0};
 for(scenario=0;scenario<11;scenario++){
  memset(memory,0x7c,sizeof memory);uint32_t result=tribes_time_caps(base,4,8);
  if(expected[scenario]){uint32_t caps[2];memcpy(caps,memory+4,8);assert(!result&&caps[0]==expected[scenario]&&caps[1]==65535);assert(!tribes_time_begin_period(caps[0]));assert(!tribes_time_end_period(caps[0]));}
  else {assert(result==97);for(unsigned j=0;j<32;j++)assert(memory[j]==0x7c);assert(tribes_time_begin_period(1)==97);}
 }
 puts("PASS: eleven clock-resolution/failure cases; upward rounding, malformed/zero resolution and output preservation");
}
