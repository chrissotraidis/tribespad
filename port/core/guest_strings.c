/* Byte-string Win32 APIs used by original Miles/engine messages. Guest memory
   is accessed through ordinary instrumented loads/stores, never host strlen
   or strcat. Caller owns sufficient storage; overlapping copies are gated. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "guest_heap.h"
static _Noreturn void stop(void){fputs("TribesPad: unsupported Win32 string range/overlap\n",stderr);abort();}
static unsigned char *at(uint64_t base,uint64_t address){if(!address||address>UINT32_MAX||base>UINTPTR_MAX-address)stop();return (unsigned char *)(uintptr_t)(base+address);}
uint32_t tribes_lstrlen_a(uint64_t base,uint32_t address){if(!address)return 0;uint32_t n=0;while(*at(base,(uint64_t)address+n)){if(n==INT32_MAX)stop();n++;}return n;}
static uint32_t copy(uint64_t base,uint32_t dst,uint32_t src,int append){
 if(!dst||!src){tribes_set_last_error(87);return 0;}
 uint64_t n=tribes_lstrlen_a(base,src),start=(uint64_t)dst+(append?tribes_lstrlen_a(base,dst):0),end=start+n+1;
 if(end>UINT64_C(0x100000000)||(start<(uint64_t)src+n+1&&src<end))stop();
 for(uint64_t i=0;i<=n;i++)*at(base,start+i)=*at(base,(uint64_t)src+i);return dst;
}
uint32_t tribes_lstrcat_a(uint64_t base,uint32_t dst,uint32_t src){return copy(base,dst,src,1);}
uint32_t tribes_lstrcpy_a(uint64_t base,uint32_t dst,uint32_t src){return copy(base,dst,src,0);}
