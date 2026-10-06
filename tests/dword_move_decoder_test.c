#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void bad(const unsigned char *b,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
int main(void){unsigned total=0;
for(unsigned w=0;w<2;++w)for(unsigned op=0;op<2;++op)for(unsigned d=0;d<8;++d)for(unsigned merge=0;merge<(op?8:1);++merge)for(unsigned r=0;r<9;++r)for(unsigned imm=0;imm<256;++imm) {
 unsigned char b[]={0xc4,0xe3,(unsigned char)((w<<7)|((15-merge)<<3)|1),(unsigned char)(op?0x22:0x16),(unsigned char)((r==8?0:0xc0)|(d<<3)|(r==8?6:r)),(unsigned char)imm,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b,sizeof b);
 assert(ud_disassemble(&u)==6&&u.mnemonic==(op?UD_Ivpinsrd:UD_Ivpextrd));
 if(op){assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==UD_R_XMM0+d&&u.operand[0].size==128);assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_XMM0+merge&&u.operand[1].size==128);assert(u.operand[2].type==(r==8?UD_OP_MEM:UD_OP_REG)&&u.operand[2].base==(r==8?UD_R_ESI:UD_R_EAX+r)&&u.operand[2].size==32);assert(u.operand[3].type==UD_OP_IMM&&u.operand[3].lval.ubyte==imm);}
 else{assert(u.operand[0].type==(r==8?UD_OP_MEM:UD_OP_REG)&&u.operand[0].base==(r==8?UD_R_ESI:UD_R_EAX+r)&&u.operand[0].size==32);assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_XMM0+d&&u.operand[1].size==128);assert(u.operand[2].type==UD_OP_IMM&&u.operand[2].lval.ubyte==imm&&u.operand[3].type==UD_NONE);}
 assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[2].type==UD_NONE);if(!imm)for(unsigned n=1;n<6;++n)bad(b,n);++total;
}
for(unsigned op=0;op<2;++op)for(unsigned p=0;p<256;++p) {
 unsigned valid=op?((p&7)==1&&((~p>>3)&15)<8):((p&0x7f)==0x79);
 if(!valid){unsigned char b[]={0xc4,0xe3,(unsigned char)p,(unsigned char)(op?0x22:0x16),0xc0,1};bad(b,6);}
}
const unsigned char prefix[]={0x66,0x67,0xf0,0xf2,0xf3};for(unsigned j=0;j<sizeof prefix;++j){unsigned char b[]={prefix[j],0xc4,0xe3,0x79,0x16,0xc0,1};if(prefix[j]!=0x67)bad(b,7);}
printf("PASS: %u VEX dword decoder cases, allroles/imm8/WIG, reset/truncation/reserved forms\n",total);return 0;}
