#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned source,unsigned dest,unsigned memory,unsigned order) {
 unsigned char raw[]={0xc4,0xe3,0x7d,0x39,(memory?0:0xc0)|(source<<3)|dest,order,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,sizeof raw);assert(ud_disassemble(&u)==6&&u.mnemonic==UD_Ivextracti128);
 assert(u.operand[0].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[0].base==(memory?UD_R_ESI:UD_R_XMM0+dest)&&u.operand[0].size==128);
 assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_YMM0+source&&u.operand[1].size==256);
 assert(u.operand[2].type==UD_OP_IMM&&u.operand[2].size==8&&u.operand[2].lval.ubyte==order&&u.operand[3].type==UD_NONE);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE);
 for(unsigned i=1;i<6;i++)invalid(raw,i);
}
int main(void){
 for(unsigned s=0;s<8;s++)for(unsigned i=0;i<256;i++){for(unsigned d=0;d<8;d++)positive(s,d,0,i);positive(s,6,1,i);}
 invalid((const unsigned char *)"\xc4\xe3\x79\x39\xc0\x01",6);invalid((const unsigned char *)"\xc4\xe3\xfd\x39\xc0\x01",6);invalid((const unsigned char *)"\xc4\xe3\x75\x39\xc0\x01",6);invalid((const unsigned char *)"\xc4\xe3\x7c\x39\xc0\x01",6);
 invalid((const unsigned char *)"\xc4\xe2\x7d\x39\xc0\x01",6);invalid((const unsigned char *)"\xc4\xc3\x7d\x39\xc0\x01",6);
 invalid((const unsigned char *)"\xf0\xc4\xe3\x7d\x39\xc0\x01",7);invalid((const unsigned char *)"\x66\xc4\xe3\x7d\x39\xc0\x01",7);
 puts("PASS: VEXTRACTI128 everyimm8/allroles XMMdestination/YMMsource/m128, W0/L1/66/reservedvvvv, truncation/reset/unsupportedforms");return 0;
}
