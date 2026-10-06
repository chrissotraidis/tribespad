#include "udis86.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 unsigned count=0;
 for(unsigned opcode=0x10;opcode<=0x11;opcode++)for(unsigned dst=0;dst<8;dst++)for(unsigned src=0;src<8;src++){
  unsigned char raw[]={0xf2,0x0f,opcode,0xc0|(dst<<3)|src,0xa5,0xf3,0xa5};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,sizeof raw);
  assert(ud_disassemble(&u)==4&&u.mnemonic==UD_Imovsd);
  assert(u.operand[0].type==UD_OP_REG&&u.operand[0].size==128&&u.operand[0].base==UD_R_XMM0+(opcode==0x10?dst:src));
  assert(u.operand[1].type==UD_OP_REG&&u.operand[1].size==128&&u.operand[1].base==UD_R_XMM0+(opcode==0x10?src:dst));
  assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Imovsd&&u.operand[0].type==UD_NONE&&u.operand[1].type==UD_NONE&&!u.pfx_rep);
  assert(ud_disassemble(&u)==2&&u.mnemonic==UD_Imovsd&&u.operand[0].type==UD_NONE&&u.operand[1].type==UD_NONE&&u.pfx_rep);count++;
 }
 for(unsigned opcode=0x10;opcode<=0x11;opcode++)for(unsigned reg=0;reg<8;reg++){
  unsigned char raw[]={0xf2,0x0f,opcode,(reg<<3)|4,0x24};ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,sizeof raw);
  assert(ud_disassemble(&u)==5&&u.mnemonic==UD_Imovsd);unsigned memory=opcode==0x11?0:1;
  assert(u.operand[memory].type==UD_OP_MEM&&u.operand[memory].size==64&&u.operand[memory].base==UD_R_ESP);
  assert(u.operand[1-memory].base==UD_R_XMM0+reg&&u.operand[1-memory].size==128);count++;
 }
 printf("PASS: %u legacy MOVSD register/opcode roles, exact scalar memory widths and sequential scalar/string/REP decode resets\n",count);
}
