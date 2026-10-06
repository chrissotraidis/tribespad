#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void bad(const unsigned char *b,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
int main(void){unsigned count=0;
for(unsigned w=0;w<2;w++)for(unsigned wide=0;wide<2;wide++)for(unsigned d=0;d<8;d++)for(unsigned l=0;l<8;l++)for(unsigned r=0;r<9;r++)for(unsigned imm=0;imm<256;imm++){
unsigned char b[]={0xc4,0xe3,(unsigned char)((w<<7)|((15-l)<<3)|(wide<<2)|1),0x44,(unsigned char)((r==8?0:0xc0)|(d<<3)|(r==8?6:r)),(unsigned char)imm,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b,sizeof b);assert(ud_disassemble(&u)==6&&u.mnemonic==UD_Ivpclmulqdq);
unsigned base=wide?UD_R_YMM0:UD_R_XMM0,bits=wide?256:128;
assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==base+d&&u.operand[0].size==bits);
assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==base+l&&u.operand[1].size==bits);
assert(u.operand[2].type==(r==8?UD_OP_MEM:UD_OP_REG)&&u.operand[2].base==(r==8?UD_R_ESI:base+r)&&u.operand[2].size==bits);
assert(u.operand[3].type==UD_OP_IMM&&u.operand[3].size==8&&u.operand[3].lval.ubyte==imm);
assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[3].type==UD_NONE);
if(!imm)for(unsigned n=1;n<6;n++)bad(b,n);count++;
}
bad((const unsigned char *)"\xc4\xe3\x78\x44\xc0\x01",6);bad((const unsigned char *)"\xc4\xe3\x7b\x44\xc0\x01",6);bad((const unsigned char *)"\xc4\xe1\x79\x44\xc0\x01",6);bad((const unsigned char *)"\xc4\xc3\x79\x44\xc0\x01",6);bad((const unsigned char *)"\x66\xc4\xe3\x79\x44\xc0\x01",7);bad((const unsigned char *)"\xf0\xc4\xe3\x79\x44\xc0\x01",7);
printf("PASS: %u VPCLMULQDQ decoder cases, all imm8/roles/widths/WIG, reset/truncation/invalid forms\n",count);return 0;}
