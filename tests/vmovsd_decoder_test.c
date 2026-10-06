#include "udis86.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned form,unsigned store,unsigned memory){
 unsigned char raw[]={0xc4,0xe1,(memory?0x7b:0x73)|(form==2?0x80:0),store?0x11:0x10,memory?(store?0x16:0x06):(store?0xd0:0xc2),0xc3};if(form==0){raw[1]=0xc5;raw[2]|=0x80;}unsigned char *bytes=raw+(form==0);unsigned n=form==0?4:5;ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_syntax(&u,UD_SYN_INTEL);ud_set_input_buffer(&u,bytes,n+1);ud_set_pc(&u,0x1000);
 assert(ud_disassemble(&u)==n&&u.mnemonic==UD_Ivmovsd);
 if(memory){unsigned m=store?0:1;assert(u.operand[m].type==UD_OP_MEM&&u.operand[m].size==64&&u.operand[m].base==UD_R_ESI);assert(u.operand[1-m].type==UD_OP_REG&&u.operand[1-m].size==128&&u.operand[1-m].base==(store?UD_R_XMM2:UD_R_XMM0));assert(u.operand[2].type==UD_NONE);}
 else{assert(u.operand[0].base==UD_R_XMM0&&u.operand[1].base==UD_R_XMM1&&u.operand[2].base==UD_R_XMM2);for(unsigned i=0;i<3;i++)assert(u.operand[i].type==UD_OP_REG&&u.operand[i].size==128);}
 assert(u.operand[3].type==UD_NONE);assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE&&!ud_disassemble(&u));for(unsigned length=1;length<n;length++)invalid(bytes,length);
}
int main(void){
 for(unsigned form=0;form<3;form++)for(unsigned store=0;store<2;store++)for(unsigned memory=0;memory<2;memory++)positive(form,store,memory);
 invalid((const unsigned char *)"\xc5\xf3\x10\x06",4);invalid((const unsigned char *)"\xc5\xf3\x11\x06",4);invalid((const unsigned char *)"\xc5\xff\x10\x06",4);invalid((const unsigned char *)"\xc5\xf7\x10\xc2",4);invalid((const unsigned char *)"\xf0\xc5\xfb\x10\x06",5);invalid((const unsigned char *)"\x66\xc5\xfb\x10\x06",5);
 puts("PASS: VMOVSD register opcode roles, memory widths/reserved vvvv, C5/C4 WIG, truncation and sequential decoder reset");return 0;
}
