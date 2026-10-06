#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned form,unsigned wide,unsigned op,unsigned dest,unsigned left,unsigned right,unsigned memory) {
 unsigned char bytes[6];unsigned n=0;bytes[n++]=form?0xc4:0xc5;if(form)bytes[n++]=op==0?0xe2:0xe1;
 bytes[n++]=((15-left)<<3)|1|(wide<<2)|((!form||form==2)?0x80:0);bytes[n++]=(unsigned char[]){0x04,0xf5,0xfe,0xfc}[op];bytes[n++]=(memory?0:0xc0)|(dest<<3)|right;
 unsigned length=n;bytes[n++]=0xc3;ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,bytes,n);assert(ud_disassemble(&u)==length&&u.mnemonic==((enum ud_mnemonic_code[]){UD_Ivpmaddubsw,UD_Ivpmaddwd,UD_Ivpaddd,UD_Ivpaddb})[op]);
 unsigned base=wide?UD_R_YMM0:UD_R_XMM0,bits=wide?256:128;
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==base+dest&&u.operand[0].size==bits);
 assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==base+left&&u.operand[1].size==bits);
 assert(u.operand[2].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[2].base==(memory?UD_R_ESI:base+right)&&u.operand[2].size==bits&&u.operand[3].type==UD_NONE);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 for(unsigned i=1;i<length;i++)invalid(bytes,i);
}
int main(void){
 for(unsigned f=0;f<3;f++)for(unsigned w=0;w<2;w++)for(unsigned op=0;op<4;op++)for(unsigned d=0;d<8;d++)for(unsigned l=0;l<8;l++){
  if(op==0&&f==0)continue;
  for(unsigned r=0;r<8;r++)positive(f,w,op,d,l,r,0);positive(f,w,op,d,l,6,1);
 }
 invalid((const unsigned char *)"\xc5\xf8\xf5\xc0",4);invalid((const unsigned char *)"\xc5\xfb\xfe\xc0",4);
 invalid((const unsigned char *)"\xc4\xe2\x79\xf5\xc0",5);invalid((const unsigned char *)"\xc4\xc1\x79\xfe\xc0",5);
 invalid((const unsigned char *)"\xf0\xc5\xf9\xf5\xc0",5);invalid((const unsigned char *)"\x66\xc5\xf9\xfe\xc0",5);
 puts("PASS: packed multiply-add/add C5/C4 WIG 128/256 all roles and memory widths, unsupported selectors/maps/extended roles, truncation and reset");return 0;
}
