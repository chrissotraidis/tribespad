#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned dest,unsigned indices,unsigned source,unsigned memory) {
 unsigned char raw[]={0xc4,0xe2,((15-indices)<<3)|5,0x36,(memory?0:0xc0)|(dest<<3)|source,0xc3};
 ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,sizeof raw);assert(ud_disassemble(&u)==5&&u.mnemonic==UD_Ivpermd);
 for(unsigned i=0;i<3;i++)assert(u.operand[i].size==256);
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==UD_R_YMM0+dest);
 assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_YMM0+indices);
 assert(u.operand[2].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[2].base==(memory?UD_R_ESI:UD_R_YMM0+source)&&u.operand[3].type==UD_NONE);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 for(unsigned i=1;i<5;i++)invalid(raw,i);
}
int main(void){
 for(unsigned d=0;d<8;d++)for(unsigned i=0;i<8;i++){for(unsigned s=0;s<8;s++)positive(d,i,s,0);positive(d,i,6,1);}
 invalid((const unsigned char *)"\xc4\xe2\x79\x36\xc0",5);invalid((const unsigned char *)"\xc4\xe2\xfd\x36\xc0",5);
 invalid((const unsigned char *)"\xc4\xe2\x7c\x36\xc0",5);invalid((const unsigned char *)"\xc4\xe2\x7f\x36\xc0",5);
 invalid((const unsigned char *)"\xc4\xe3\x7d\x36\xc0",5);invalid((const unsigned char *)"\xc4\xc2\x7d\x36\xc0",5);
 invalid((const unsigned char *)"\xf0\xc4\xe2\x7d\x36\xc0",6);invalid((const unsigned char *)"\x66\xc4\xe2\x7d\x36\xc0",6);
 invalid((const unsigned char *)"\xc5\xfd\x36\xc0",4);
 puts("PASS: VPERMD C4 W0/L1/66 all YMM roles and m256 sources, invalid prefix/width/map/extended forms, truncation and reset");return 0;
}
