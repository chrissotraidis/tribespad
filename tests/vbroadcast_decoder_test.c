#include "udis86.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned kind,unsigned wide,unsigned memory){
 unsigned char bytes[]={0xc4,0xe2,0x79|(wide?4:0),0x18+kind,memory?0x06:0xc2,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_syntax(&u,UD_SYN_INTEL);ud_set_input_buffer(&u,bytes,sizeof bytes);ud_set_pc(&u,0x1000);
 assert(ud_disassemble(&u)==5&&u.mnemonic==(kind==0?UD_Ivbroadcastss:kind==1?UD_Ivbroadcastsd:UD_Ivbroadcastf128));
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==(wide?UD_R_YMM0:UD_R_XMM0)&&u.operand[0].size==(wide?256:128));
 if(memory)assert(u.operand[1].type==UD_OP_MEM&&u.operand[1].base==UD_R_ESI&&u.operand[1].size==(32u<<kind));
 else assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_XMM2&&u.operand[1].size==128);
 assert(u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE&&!ud_disassemble(&u));
 for(unsigned n=1;n<5;n++)invalid(bytes,n);
}
int main(void){
 for(unsigned wide=0;wide<2;wide++)for(unsigned memory=0;memory<2;memory++)positive(0,wide,memory);
 for(unsigned memory=0;memory<2;memory++)positive(1,1,memory);positive(2,1,1);
 static const unsigned char rejected[][6]={
 {0xc4,0xe2,0xf9,0x18,0x06},{0xc4,0xe2,0x78,0x18,0x06},{0xc4,0xe2,0x7a,0x18,0x06},{0xc4,0xe2,0x7b,0x18,0x06},
 {0xc4,0xe2,0x71,0x18,0x06},{0xc4,0xe2,0x79,0x19,0x06},{0xc4,0xe2,0x79,0x1a,0x06},{0xc4,0xe2,0x7d,0x1a,0xc2},
 {0xc4,0xe2,0x79,0x17,0x06},{0xc4,0xc2,0x79,0x18,0x06},{0xc4,0xe3,0x79,0x18,0x06}
 };
 for(unsigned i=0;i<sizeof rejected/sizeof rejected[0];i++)invalid(rejected[i],5);
 invalid((const unsigned char *)"\xf0\xc4\xe2\x79\x18\x06",6);
 invalid((const unsigned char *)"\x66\xc4\xe2\x79\x18\x06",6);
 invalid((const unsigned char *)"\xf3\xc4\xe2\x79\x18\x06",6);
 puts("PASS: broadcast widths/roles, reserved selectors/maps/source forms, truncation, legacy prefixes and sequential reset");return 0;
}
