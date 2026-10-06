#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned form,unsigned op,unsigned dest,unsigned merge,unsigned source,unsigned memory) {
 unsigned char bytes[6];unsigned n=0;bytes[n++]=form?0xc4:0xc5;if(form)bytes[n++]=0xe1;
 unsigned v=op==2?0x79:((15-merge)<<3)|2;bytes[n++]=v|((!form||form==2)?0x80:0);
 bytes[n++]=op==2?0x2f:op?0x5f:0x5d;bytes[n++]=(memory?0:0xc0)|(dest<<3)|source;
 unsigned length=n;bytes[n++]=0xc3;ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,bytes,n);
 assert(ud_disassemble(&u)==length&&u.mnemonic==(op==2?UD_Ivcomisd:op?UD_Ivmaxss:UD_Ivminss));
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==UD_R_XMM0+dest&&u.operand[0].size==128);
 unsigned slot=op==2?1:2;
 if(op!=2)assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_XMM0+merge&&u.operand[1].size==128);
 assert(u.operand[slot].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[slot].base==(memory?UD_R_ESI:UD_R_XMM0+source)&&u.operand[slot].size==(memory?(op==2?64:32):128));
 assert(u.operand[3].type==UD_NONE&&(op!=2||u.operand[2].type==UD_NONE));
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 for(unsigned i=1;i<length;i++)invalid(bytes,i);
}
int main(void){
 for(unsigned f=0;f<3;f++)for(unsigned op=0;op<3;op++)for(unsigned d=0;d<8;d++)for(unsigned m=0;m<(op==2?1:8);m++){
  for(unsigned s=0;s<8;s++)positive(f,op,d,m,s,0);positive(f,op,d,m,6,1);
 }
 const unsigned char bad[][5]={{0xc5,0xfe,0x5d,0xc0},{0xc5,0xfe,0x5f,0xc0},{0xc5,0xf9,0x5d,0xc0},{0xc5,0xfb,0x5f,0xc0},{0xc5,0xf1,0x2f,0xc0},{0xc5,0xfd,0x2f,0xc0},{0xc4,0xe2,0x7a,0x5d,0xc0},{0xc4,0xc1,0x7a,0x5f,0xc0}};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)invalid(bad[i],i<6?4:5);
 invalid((const unsigned char *)"\xf0\xc5\xfa\x5d\xc0",5);invalid((const unsigned char *)"\x66\xc5\xfa\x5f\xc0",5);invalid((const unsigned char *)"\xf3\xc5\xf9\x2f\xc0",5);
 puts("PASS: scalar MIN/MAX/COMISD C5/C4 WIG L0 all roles, memory widths, unsupported selectors/length/maps, reserved comparison vvvv, truncation and reset");return 0;
}
