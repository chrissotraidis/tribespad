#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned op,unsigned w,unsigned dest,unsigned source,unsigned count,unsigned memory) {
 unsigned char raw[]={0xc4,0xe2,(w<<7)|((15-count)<<3)|(op?3:1),0xf7,(memory?0:0xc0)|(dest<<3)|source,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,sizeof raw);assert(ud_disassemble(&u)==5&&u.mnemonic==(op?UD_Ishrx:UD_Ishlx));
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==UD_R_EAX+dest&&u.operand[0].size==32);
 assert(u.operand[1].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[1].base==(memory?UD_R_ESI:UD_R_EAX+source)&&u.operand[1].size==32);
 assert(u.operand[2].type==UD_OP_REG&&u.operand[2].base==UD_R_EAX+count&&u.operand[2].size==32&&u.operand[3].type==UD_NONE);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE);
 for(unsigned i=1;i<5;i++)invalid(raw,i);
}
int main(void){
 for(unsigned op=0;op<2;op++)for(unsigned w=0;w<2;w++)for(unsigned d=0;d<8;d++)for(unsigned c=0;c<8;c++){for(unsigned s=0;s<8;s++)positive(op,w,d,s,c,0);positive(op,w,d,6,c,1);}
 invalid((const unsigned char *)"\xc4\xe2\x7d\xf7\xc0",5);invalid((const unsigned char *)"\xc4\xe2\x7f\xf7\xc0",5);invalid((const unsigned char *)"\xc4\xe2\x78\xf7\xc0",5);invalid((const unsigned char *)"\xc4\xe2\x7a\xf7\xc0",5);
 invalid((const unsigned char *)"\xc4\xe3\x79\xf7\xc0",5);invalid((const unsigned char *)"\xc4\xc2\x79\xf7\xc0",5);invalid((const unsigned char *)"\xc4\xe2\x39\xf7\xc0",5);
 invalid((const unsigned char *)"\xf0\xc4\xe2\x79\xf7\xc0",6);invalid((const unsigned char *)"\x66\xc4\xe2\x79\xf7\xc0",6);
 puts("PASS: SHLX/SHRX all GPR roles, IA32 WIG/L0/66orF2, exactdword source/count, truncation/reset/unsupportedforms");return 0;
}
