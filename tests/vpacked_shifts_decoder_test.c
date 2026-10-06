#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
static void positive(unsigned form,unsigned wide,unsigned mode,unsigned dest,unsigned source,unsigned count,unsigned memory){
 unsigned char bytes[7];unsigned n=0,immediate=mode<4,variable=mode==6;
 bytes[n++]=form?0xc4:0xc5;if(form)bytes[n++]=variable?0xe2:0xe1;
 bytes[n++]=((15-(immediate?dest:source))<<3)|1|(wide<<2)|((!form||form==2)?0x80:0);
 bytes[n++]=immediate?(mode?0x73:0x72):variable?0x47:mode==4?0xf2:0xd3;
 bytes[n++]=(memory?0:0xc0)|((immediate?(unsigned[]){6,2,7,3}[mode]:dest)<<3)|(memory?6:immediate?source:count);
 if(immediate)bytes[n++]=(unsigned char)count;
 unsigned length=n;bytes[n++]=0xc3;ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,bytes,n);
 enum ud_mnemonic_code mnemonic=((enum ud_mnemonic_code[]){UD_Ivpslld,UD_Ivpsrlq,UD_Ivpslldq,UD_Ivpsrldq,UD_Ivpslld,UD_Ivpsrlq,UD_Ivpsllvd})[mode];
 assert(ud_disassemble(&u)==length&&u.mnemonic==mnemonic);unsigned base=wide?UD_R_YMM0:UD_R_XMM0,bits=wide?256:128;
 assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==base+dest&&u.operand[0].size==bits);
 assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==base+source&&u.operand[1].size==bits);
 if(immediate)assert(u.operand[2].type==UD_OP_IMM&&u.operand[2].size==8&&u.operand[2].lval.ubyte==count);
 else assert(u.operand[2].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[2].base==(memory?UD_R_ESI:(variable?base:UD_R_XMM0)+count)&&u.operand[2].size==(variable?bits:128));
 assert(u.operand[3].type==UD_NONE);assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
 for(unsigned i=1;i<length;i++)invalid(bytes,i);
}
int main(void){
 for(unsigned f=0;f<3;f++)for(unsigned w=0;w<2;w++)for(unsigned m=0;m<7;m++)for(unsigned d=0;d<8;d++)for(unsigned s=0;s<8;s++){
  if(m==6&&f!=1)continue;
  if(m<4)for(unsigned k=0;k<9;k++)positive(f,w,m,d,s,(unsigned[]){0,1,15,16,31,32,63,64,255}[k],0);
  else {for(unsigned k=0;k<8;k++)positive(f,w,m,d,s,k,0);positive(f,w,m,d,s,0,1);}
 }
 invalid((const unsigned char *)"\xc5\xf9\x72\xc0\x05",5);invalid((const unsigned char *)"\xc5\xf9\x72\x30\x05",5);
 invalid((const unsigned char *)"\xc5\xf8\x73\xd0\x08",5);invalid((const unsigned char *)"\xc4\xe2\xf9\x47\xc0",5);
 invalid((const unsigned char *)"\xc4\xe3\x79\xd3\xc0",5);invalid((const unsigned char *)"\xc4\xc1\x79\xd3\xc0",5);
 puts("PASS: packed shift immediate destination-vvvv roles, scalar128-bit/per-lane counts,128/256 widths, WIG versus variableW0, groups/truncation/reset");return 0;
}
