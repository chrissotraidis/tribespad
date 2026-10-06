#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned form,unsigned quad,unsigned store,unsigned d,unsigned s,unsigned memory){
 unsigned char bytes[7];unsigned n=0;bytes[n++]=form?0xc4:0xc5;if(form)bytes[n++]=0xe1;
 bytes[n++]=0x78|(quad&&!store?2:1)|((!form||form==2)?0x80:0);
 bytes[n++]=quad?(store?0xd6:0x7e):(store?0x7e:0x6e);
 bytes[n++]=(memory?0:0xc0)|((store?s:d)<<3)|(memory?6:store?d:s);
 unsigned length=n;bytes[n++]=0xc3;ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,bytes,n);
 assert(ud_disassemble(&u)==length&&u.mnemonic==(quad?UD_Ivmovq:UD_Ivmovd));
 unsigned vector=store?1:0,other=1-vector;
 assert(u.operand[vector].type==UD_OP_REG&&u.operand[vector].base==UD_R_XMM0+(store?s:d)&&u.operand[vector].size==128);
 assert(u.operand[other].type==(memory?UD_OP_MEM:UD_OP_REG));
 assert(u.operand[other].base==(memory?UD_R_ESI:(quad?UD_R_XMM0:UD_R_EAX)+(store?d:s)));
 assert(u.operand[other].size==(memory?(quad?64:32):quad?128:32));
 assert(u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 for(unsigned i=1;i<length;i++)invalid(bytes,i);
}
int main(void){
 for(unsigned f=0;f<3;f++)for(unsigned q=0;q<2;q++)for(unsigned st=0;st<2;st++)for(unsigned d=0;d<8;d++)for(unsigned s=0;s<8;s++)positive(f,q,st,d,s,0);
 for(unsigned f=0;f<3;f++)for(unsigned q=0;q<2;q++)for(unsigned st=0;st<2;st++)for(unsigned v=0;v<8;v++)positive(f,q,st,v,v,1);
 invalid((const unsigned char *)"\xc5\xf1\x6e\xc0",4);invalid((const unsigned char *)"\xc5\xf2\x7e\xc0",4);
 invalid((const unsigned char *)"\xc5\xfd\x7e\xc0",4);invalid((const unsigned char *)"\xc5\xfd\xd6\xc0",4);
 invalid((const unsigned char *)"\xc5\xf8\x6e\xc0",4);invalid((const unsigned char *)"\xc5\xfb\xd6\xc0",4);
 invalid((const unsigned char *)"\xc4\xe3\x79\x6e\xc0",5);invalid((const unsigned char *)"\xc4\xc1\x7a\x7e\xc0",5);
 puts("PASS: VMOVD/VMOVQ both opcode directions, all GPR/XMM roles, exact memory widths, C5/C4 IA32 WIG, reserved selectors, truncation and reset");return 0;
}
