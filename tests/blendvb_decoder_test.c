#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned wide,unsigned dest,unsigned left,unsigned right,unsigned selector,unsigned memory){
 unsigned char raw[]={0xc4,0xe3,((15-left)<<3)|1|wide*4,0x4c,(memory?0:0xc0)|(dest<<3)|right,selector,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,sizeof raw);assert(ud_disassemble(&u)==6&&u.mnemonic==UD_Ivpblendvb);unsigned base=wide?UD_R_YMM0:UD_R_XMM0;
 for(unsigned i=0;i<4;i++)assert(u.operand[i].size==(wide?256:128));
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==base+dest);assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==base+left);
 assert(u.operand[2].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[2].base==(memory?UD_R_ESI:base+right));assert(u.operand[3].type==UD_OP_REG&&u.operand[3].base==base+((selector>>4)&7));
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);for(unsigned i=1;i<6;i++)invalid(raw,i);
}
int main(void){
 for(unsigned w=0;w<2;w++)for(unsigned d=0;d<8;d++)for(unsigned l=0;l<8;l++)for(unsigned m=0;m<8;m++)for(unsigned ignored=0;ignored<4;ignored++){unsigned s=m*16+(ignored&1?15:0)+(ignored&2?128:0);for(unsigned r=0;r<8;r++)positive(w,d,l,r,s,0);positive(w,d,l,6,s,1);}
 for(unsigned w=0;w<2;w++)for(unsigned s=0;s<256;s++){positive(w,0,1,2,s,0);positive(w,0,1,6,s,1);}
 invalid((const unsigned char *)"\xc4\xe3\xf9\x4c\xc0\x20",6);invalid((const unsigned char *)"\xc4\xe3\x78\x4c\xc0\x20",6);invalid((const unsigned char *)"\xc4\xe2\x79\x4c\xc0\x20",6);invalid((const unsigned char *)"\xc4\xc3\x79\x4c\xc0\x20",6);
 invalid((const unsigned char *)"\xf0\xc4\xe3\x79\x4c\xc0\x20",7);invalid((const unsigned char *)"\x66\xc4\xe3\x79\x4c\xc0\x20",7);
 puts("PASS: VPBLENDVB bothwidths/all4roles/is4 IA32ignoredbit7andlownibble, W0/66, truncation/reset/unsupportedforms");return 0;
}
