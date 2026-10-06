#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
static uint32_t state=0x3f87c51a;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
/* Independent coefficient parity rather than shifted partial products. */
static void multiply(unsigned char *out,uint64_t a,uint64_t b){
 memset(out,0,16);
 for(unsigned degree=0;degree<127;degree++){
  unsigned parity=0,first=degree>63?degree-63:0,last=degree<63?degree:63;
  for(unsigned i=first;i<=last;i++)parity^=(unsigned)((a>>i)&(b>>(degree-i))&1);
  out[degree/8]|=(unsigned char)(parity<<(degree%8));
 }
}
int main(void){unsigned long long count=0;unsigned char mxcsr[8192]={0};
static const uint64_t edge[]={0,1,UINT64_MAX,UINT64_C(0x8000000000000000),UINT64_C(0xaaaaaaaaaaaaaaaa),UINT64_C(0x5555555555555555),UINT64_C(0xffffffff),UINT64_C(0xffffffff00000000)};
for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<(block==0?4096:16);iteration++){
 const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],a[32],b[32],av[32],bv[32],result[32];unsigned char *memory=backing+17;
 for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
 for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
 cpu._mxcsr=(block*16+iteration)&65535;mxcsr[cpu._mxcsr/8]|=1u<<(cpu._mxcsr%8);
 unsigned n=c->wide?32:16,offset=c->address==3?3:c->address==5?0:iteration%(65-n);
 for(unsigned j=0;j<32;j+=8){uint64_t x=block==0?UINT64_C(1)<<(iteration%64):edge[(iteration+j/8)%8];uint64_t y=block==0?UINT64_C(1)<<(iteration/64):edge[(iteration*3+j/8)%8];
 if(block&&iteration>=8){x=((uint64_t)random_word()<<32)|random_word();y=((uint64_t)random_word()<<32)|random_word();}memcpy(av+j,&x,8);memcpy(bv+j,&y,8);}
 memcpy(cpu._xmm[c->left],av,16);if(c->wide)memcpy(cpu._ymm_hi[c->left],av+16,16);
 cpu._pointer_offset=(uintptr_t)memory-(c->address==5?c->disp:0x200000);
 if(c->kind){memcpy(memory+offset,bv,n);
 if(c->address==0)cpu._eax=0x200000+offset-c->disp;
 else if(c->address==1)cpu._esp=0x200000+offset-c->disp;
 else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
 else if(c->address==4)cpu._ecx=0x200000+offset-c->disp;
 else if(c->address==6)cpu._edx=0x200000+offset-c->disp;
 else if(c->address==7)cpu._esi=0x200000+offset-c->disp;
 memcpy(b,memory+offset,n);
 }else{memcpy(cpu._xmm[c->right],bv,16);if(c->wide)memcpy(cpu._ymm_hi[c->right],bv+16,16);memcpy(b,cpu._xmm[c->right],16);if(c->wide)memcpy(b+16,cpu._ymm_hi[c->right],16);}
 memcpy(a,cpu._xmm[c->left],16);if(c->wide)memcpy(a+16,cpu._ymm_hi[c->left],16);
 for(unsigned lane=0;lane<n;lane+=16){uint64_t x,y;memcpy(&x,a+lane+(c->control&1)*8,8);memcpy(&y,b+lane+((c->control>>4)&1)*8,8);multiply(result+lane,x,y);}
 expected=cpu;memcpy(wanted,backing,sizeof wanted);memcpy(expected._xmm[c->dest],result,16);if(c->wide)memcpy(expected._ymm_hi[c->dest],result+16,16);else memset(expected._ymm_hi[c->dest],0,16);
 bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));count++;
}
for(unsigned j=0;j<sizeof mxcsr;j++)assert(mxcsr[j]==255);
printf("PASS: %llu emitted VPCLMULQDQ cases, polynomial coefficient parity/full CPU/flags/MXCSR/aliases/guards/selectors\n",count);return 0;}
