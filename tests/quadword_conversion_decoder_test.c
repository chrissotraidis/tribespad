#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static const unsigned mnemonics[]={UD_Ivcvttps2uqq,UD_Ivcvttps2qq,UD_Ivcvttpd2uqq,UD_Ivcvttpd2qq,UD_Ivcvtuqq2pd,UD_Ivcvtqq2pd,UD_Ivcvtqq2ps};
static const unsigned char selector[]={0x7d,0x7d,0xfd,0xfd,0xfe,0xfe,0xfc},opcode[]={0x78,0x7a,0x78,0x7a,0x7a,0xe6,0x5b};
static void bad(const unsigned char *b,unsigned n){ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b,n);assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);}
int main(void) {
 unsigned total=0;
 for(unsigned op=0;op<7;++op)for(unsigned d=0;d<8;++d) {
  for(unsigned r=0;r<8;++r) {
   unsigned char b[]={0x62,0xf1,selector[op],8,opcode[op],(unsigned char)(0xc0|d<<3|r),0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b,sizeof b);
   assert(ud_disassemble(&u)==6&&u.mnemonic==mnemonics[op]);assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==UD_R_XMM0+d&&u.operand[0].size==128);
   assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_XMM0+r&&u.operand[1].size==128&&u.operand[2].type==UD_NONE);
   assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE);
   for(unsigned n=1;n<6;++n)bad(b,n);++total;
  }
  for(unsigned sib=0;sib<2;++sib)for(unsigned disp=0;disp<256;++disp) {
   unsigned char b[]={0x62,0xf1,selector[op],8,opcode[op],(unsigned char)(0x40|d<<3|(sib?4:0)),0xb3,(unsigned char)disp,0xc3};if(!sib)b[6]=(unsigned char)disp;
   ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b,sib?9:8);unsigned n=sib?8:7;
   assert(ud_disassemble(&u)==n&&u.mnemonic==mnemonics[op]);assert(u.operand[1].type==UD_OP_MEM&&u.operand[1].size==(op<2?64:128));
   assert(u.operand[1].offset==32&&u.operand[1].lval.sdword==(int)(signed char)disp*(op<2?8:16));assert(u.operand[1].base==(sib?UD_R_EBX:UD_R_EAX));
   if(sib)assert(u.operand[1].index==UD_R_ESI&&u.operand[1].scale==4);++total;
  }
  unsigned char abs[]={0x62,0xf1,selector[op],8,opcode[op],(unsigned char)(5|d<<3),0x03,0x00,0x20,0x00};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,abs,sizeof abs);assert(ud_disassemble(&u)==10&&u.mnemonic==mnemonics[op]&&u.operand[1].type==UD_OP_MEM&&u.operand[1].offset==32&&u.operand[1].lval.udword==0x200003);++total;
 }
 for(unsigned p=0;p<256;++p)if(p!=8){unsigned char b[]={0x62,0xf1,0x7d,(unsigned char)p,0x78,0xc0};bad(b,6);}
 for(unsigned p=0xc0;p<256;++p)if(p!=0xf1){unsigned char b[]={0x62,(unsigned char)p,0x7d,8,0x78,0xc0};bad(b,6);}
 for(unsigned p=0;p<256;++p)if(p!=0x7d&&p!=0xfd){unsigned char b[]={0x62,0xf1,(unsigned char)p,8,0x78,0xc0};bad(b,6);}
 const unsigned char prefixes[]={0x66,0x67,0xf0,0xf2,0xf3,0x64};for(unsigned j=0;j<sizeof prefixes;++j){unsigned char b[]={prefixes[j],0x62,0xf1,0x7d,8,0x78,0xc0};bad(b,7);}
 /* Preserve valid legacy IA-32 BOUND, including address-size override. */
 unsigned char bound[]={0x62,0x00,0xc3};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,bound,sizeof bound);assert(ud_disassemble(&u)==2&&u.mnemonic==UD_Ibound);
 unsigned char bound16[]={0x67,0x62,0x00};ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,bound16,sizeof bound16);assert(ud_disassemble(&u)==3&&u.mnemonic==UD_Ibound);
 for(unsigned xmm=0;xmm<2;++xmm)for(unsigned dest=0;dest<8;++dest)for(unsigned src=0;src<8;++src) {
  unsigned char b[]={0x66,0x0f,0x62,(unsigned char)(0xc0|dest<<3|src)};ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,b+(xmm?0:1),xmm?4:3);
  assert(ud_disassemble(&u)==(xmm?4:3)&&u.mnemonic==UD_Ipunpckldq);assert(u.operand[0].base==(xmm?UD_R_XMM0:UD_R_MM0)+dest&&u.operand[1].base==(xmm?UD_R_XMM0:UD_R_MM0)+src);++total;
 }
 printf("PASS: %u finite EVEX128 conversion decoder cases, compressed offsets, reset/truncation/negative prefixes and legacy BOUND\n",total);return 0;
}
