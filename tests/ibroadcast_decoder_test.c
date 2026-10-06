#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned kind,unsigned wide,unsigned dest,unsigned source,unsigned memory){
 unsigned op[]={0x79,0x58,0x59,0x5a};enum ud_mnemonic_code names[]={UD_Ivpbroadcastw,UD_Ivpbroadcastd,UD_Ivpbroadcastq,UD_Ivbroadcasti128};unsigned char raw[]={0xc4,0xe2,0x79|(wide*4),op[kind],(memory?0:0xc0)|(dest<<3)|source,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,sizeof raw);assert(ud_disassemble(&u)==5&&u.mnemonic==names[kind]);
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==(wide?UD_R_YMM0:UD_R_XMM0)+dest&&u.operand[0].size==(wide?256:128));
 assert(u.operand[1].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[1].base==(memory?UD_R_ESI:UD_R_XMM0+source)&&u.operand[1].size==(memory?(16u<<kind):128));assert(u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE);for(unsigned i=1;i<5;i++)invalid(raw,i);
}
int main(void){
 for(unsigned k=0;k<4;k++)for(unsigned w=k==3?1:0;w<2;w++)for(unsigned d=0;d<8;d++){if(k<3)for(unsigned s=0;s<8;s++)positive(k,w,d,s,0);positive(k,w,d,6,1);}
 invalid((const unsigned char *)"\xc4\xe2\xf9\x79\x06",5);invalid((const unsigned char *)"\xc4\xe2\x71\x79\x06",5);invalid((const unsigned char *)"\xc4\xe2\x78\x79\x06",5);
 invalid((const unsigned char *)"\xc4\xe2\x79\x5a\x06",5);invalid((const unsigned char *)"\xc4\xe2\x7d\x5a\xc0",5);invalid((const unsigned char *)"\xc4\xe1\x79\x79\x06",5);invalid((const unsigned char *)"\xc4\xc2\x79\x79\x06",5);
 invalid((const unsigned char *)"\xf0\xc4\xe2\x79\x79\x06",6);invalid((const unsigned char *)"\x66\xc4\xe2\x79\x79\x06",6);
 puts("PASS: integer W/D/Q/I128 broadcasts/allroles/widths/exactmemorysizes, W0/reservedvvvv/I128memoryonly, truncation/reset/unsupportedforms");return 0;
}
