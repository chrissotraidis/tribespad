#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned prefix,unsigned wide,unsigned dest,unsigned source){
 unsigned char raw[6];unsigned n=0;raw[n++]=prefix?0xc4:0xc5;if(prefix)raw[n++]=0xe1;raw[n++]=(prefix?0x79:0xf9)|(wide*4)|((prefix==2)*128);raw[n++]=0xd7;raw[n++]=0xc0|(dest<<3)|source;raw[n]=0xc3;
 ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n+1);assert(ud_disassemble(&u)==n&&u.mnemonic==UD_Ivpmovmskb);
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].size==32&&u.operand[0].base==UD_R_EAX+dest);
 assert(u.operand[1].type==UD_OP_REG&&u.operand[1].size==(wide?256:128)&&u.operand[1].base==(wide?UD_R_YMM0:UD_R_XMM0)+source&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE);
 for(unsigned j=1;j<n;j++)invalid(raw,j);
}
int main(void){
 for(unsigned p=0;p<3;p++)for(unsigned w=0;w<2;w++)for(unsigned d=0;d<8;d++)for(unsigned s=0;s<8;s++)positive(p,w,d,s);
 invalid((const unsigned char *)"\xc5\xf8\xd7\xc0",4);invalid((const unsigned char *)"\xc5\xf1\xd7\xc0",4);invalid((const unsigned char *)"\xc5\xf9\xd7\x00",4);invalid((const unsigned char *)"\xc4\xe2\x79\xd7\xc0",5);invalid((const unsigned char *)"\xc4\xc1\x79\xd7\xc0",5);invalid((const unsigned char *)"\xf0\xc5\xf9\xd7\xc0",5);invalid((const unsigned char *)"\x66\xc5\xf9\xd7\xc0",5);
 puts("PASS: VPMOVMSKB allGPR/vectorroles/C5C4WIG/bothwidths/66/reservedvvvv/registeronly/truncation/reset/unsupportedforms");return 0;
}
