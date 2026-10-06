#include "udis86.h"
#include <assert.h>
#include <stdio.h>
int main(void){unsigned total=0;
for(unsigned form=0;form<3;++form)for(unsigned dest=0;dest<(form==2?1:8);++dest)for(unsigned src=0;src<8;++src)for(unsigned imm=0;imm<256;++imm){
 unsigned char b[8];unsigned n=0;b[n++]=0x66;b[n++]=0x0f;if(form==0)b[n++]=0xc5;else{b[n++]=0x3a;b[n++]=0x15;}b[n++]=(form==0?0xc0|(dest<<3)|src:form==1?0xc0|(src<<3)|dest:(src<<3)|6);b[n++]=imm;b[n]=0xc3;
 ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b,n+1);assert(ud_disassemble(&u)==n&&u.mnemonic==UD_Ipextrw);assert(u.operand[0].type==(form==2?UD_OP_MEM:UD_OP_REG)&&u.operand[0].size==(form==2?16:32)&&u.operand[0].base==(form==2?UD_R_ESI:UD_R_EAX+dest));assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_XMM0+src);assert(u.operand[2].type==UD_OP_IMM&&u.operand[2].lval.ubyte==imm);assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[2].type==UD_NONE);
 if(!imm)for(unsigned t=1;t<n;++t){ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b,t);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}++total;
}
/* C4/C5 inside the secondary opcode table must remain legacy instructions. */
for(unsigned wide=0;wide<2;++wide)for(unsigned a=0;a<8;++a)for(unsigned b=0;b<8;++b){unsigned char raw[6]={0x66,0x0f,0xc4,(unsigned char)(0xc0|a<<3|b),3,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw+!wide,5-!wide);assert(ud_disassemble(&u)==5-!wide&&u.mnemonic==UD_Ipinsrw);assert(u.operand[0].base==(wide?UD_R_XMM0:UD_R_MM0)+a&&u.operand[1].base==UD_R_EAX+b);++total;}
printf("PASS: %u legacy word extraction cases and C4 decoder regressions\n",total);return 0;}
