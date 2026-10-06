#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned dest,unsigned merge,unsigned source,unsigned memory,unsigned order) {
 unsigned char raw[]={0xc4,0xe3,((15-merge)<<3)|5,0x38,(memory?0:0xc0)|(dest<<3)|source,order,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,sizeof raw);assert(ud_disassemble(&u)==6&&u.mnemonic==UD_Ivinserti128);
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==UD_R_YMM0+dest&&u.operand[0].size==256);
 assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_YMM0+merge&&u.operand[1].size==256);
 assert(u.operand[2].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[2].base==(memory?UD_R_ESI:UD_R_XMM0+source)&&u.operand[2].size==128);
 assert(u.operand[3].type==UD_OP_IMM&&u.operand[3].size==8&&u.operand[3].lval.ubyte==order);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 for(unsigned i=1;i<6;i++)invalid(raw,i);
}
int main(void){
 for(unsigned d=0;d<8;d++)for(unsigned m=0;m<8;m++)for(unsigned i=0;i<256;i++){for(unsigned s=0;s<8;s++)positive(d,m,s,0,i);positive(d,m,6,1,i);}
 invalid((const unsigned char *)"\xc4\xe3\x79\x38\xc0\x01",6);invalid((const unsigned char *)"\xc4\xe3\xfd\x38\xc0\x01",6);invalid((const unsigned char *)"\xc4\xe3\x7c\x38\xc0\x01",6);
 invalid((const unsigned char *)"\xc4\xe2\x7d\x38\xc0\x01",6);invalid((const unsigned char *)"\xc4\xc3\x7d\x38\xc0\x01",6);invalid((const unsigned char *)"\xc4\xe3\x3d\x38\xc0\x01",6);
 invalid((const unsigned char *)"\xf0\xc4\xe3\x7d\x38\xc0\x01",7);invalid((const unsigned char *)"\x66\xc4\xe3\x7d\x38\xc0\x01",7);
 puts("PASS: VINSERTI128 everyimm8/allroles YMMdestination/YMMmerge/XMMorMEM128insert, W0/L1/66, truncation/reset/unsupportedforms");return 0;
}
