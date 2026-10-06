#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned form,unsigned wide,unsigned dest,unsigned source,unsigned memory,unsigned order) {
 unsigned char raw[7];unsigned n=0;raw[n++]=form?0xc4:0xc5;if(form)raw[n++]=0xe1;raw[n++]=(form?0x79:0xf9)|(wide<<2)|(form==2?0x80:0);raw[n++]=0x70;raw[n++]=(memory?0:0xc0)|(dest<<3)|source;raw[n++]=order;unsigned length=n;raw[n++]=0xc3;
 ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)==length&&u.mnemonic==UD_Ivpshufd);unsigned base=wide?UD_R_YMM0:UD_R_XMM0;
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==base+dest&&u.operand[0].size==(wide?256:128));
 assert(u.operand[1].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[1].base==(memory?UD_R_ESI:base+source)&&u.operand[1].size==(wide?256:128));
 assert(u.operand[2].type==UD_OP_IMM&&u.operand[2].size==8&&u.operand[2].lval.ubyte==order&&u.operand[3].type==UD_NONE);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE);
 for(unsigned i=1;i<length;i++)invalid(raw,i);
}
int main(void){
 for(unsigned f=0;f<3;f++)for(unsigned w=0;w<2;w++)for(unsigned d=0;d<8;d++)for(unsigned s=0;s<8;s++)for(unsigned i=0;i<256;i++)positive(f,w,d,s,0,i);
 for(unsigned f=0;f<3;f++)for(unsigned w=0;w<2;w++)for(unsigned d=0;d<8;d++)for(unsigned i=0;i<256;i++)positive(f,w,d,6,1,i);
 invalid((const unsigned char *)"\xc5\xf1\x70\xc0\x01",5);invalid((const unsigned char *)"\xc5\xf8\x70\xc0\x01",5);invalid((const unsigned char *)"\xc5\xfb\x70\xc0\x01",5);
 invalid((const unsigned char *)"\xc4\xe3\x79\x70\xc0\x01",6);invalid((const unsigned char *)"\xc4\xc1\x79\x70\xc0\x01",6);
 invalid((const unsigned char *)"\xf0\xc5\xf9\x70\xc0\x01",6);invalid((const unsigned char *)"\x66\xc5\xf9\x70\xc0\x01",6);
 puts("PASS: VPSHUFD allimm8/allroles C5/C4 WIG bothwidths, reservedvvvv, truncation/reset and unsupportedforms");return 0;
}
