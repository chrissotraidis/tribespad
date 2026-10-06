#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n) {
    ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);
    assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);
}
static void positive(unsigned form,unsigned wide,unsigned dest,unsigned source,unsigned memory) {
    unsigned char bytes[6];unsigned n=0;
    bytes[n++]=form?0xc4:0xc5;if(form)bytes[n++]=0xe1;
    bytes[n++]=(form==1?0x78:0xf8)|(wide?4:0);bytes[n++]=0x51;
    bytes[n++]=(memory?0:0xc0)|(dest<<3)|source;
    unsigned length=n;bytes[n++]=0xc3;
    ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_syntax(&u,UD_SYN_INTEL);ud_set_input_buffer(&u,bytes,n);
    assert(ud_disassemble(&u)==length&&u.mnemonic==UD_Ivsqrtps);
    unsigned base=wide?UD_R_YMM0:UD_R_XMM0,bits=wide?256:128;
    assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==base+dest&&u.operand[0].size==bits);
    assert(u.operand[1].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[1].base==(memory?UD_R_ESI:base+source)&&u.operand[1].size==bits);
    assert(u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
    assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE&&!ud_disassemble(&u));
    for(unsigned i=1;i<length;i++)invalid(bytes,i);
}
int main(void) {
    for(unsigned f=0;f<3;f++)for(unsigned w=0;w<2;w++)for(unsigned d=0;d<8;d++) {
        for(unsigned s=0;s<8;s++)positive(f,w,d,s,0);
        positive(f,w,d,6,1);
    }
    const unsigned char rejected[][5]={
        {0xc5,0xf0,0x51,0xc0},{0xc5,0xf4,0x51,0xc0},
        {0xc5,0xf9,0x51,0xc0},{0xc5,0xfa,0x51,0xc0},
        {0xc4,0xe2,0x78,0x51,0xc0},{0xc4,0xe3,0x78,0x51,0xc0},
        {0xc4,0xc1,0x78,0x51,0xc0}
    };
    for(unsigned i=0;i<sizeof rejected/sizeof rejected[0];i++)invalid(rejected[i],i<4?4:5);
    invalid((const unsigned char *)"\xf0\xc5\xf8\x51\xc0",5);
    invalid((const unsigned char *)"\x66\xc5\xf8\x51\xc0",5);
    invalid((const unsigned char *)"\xf3\xc5\xf8\x51\xc0",5);
    puts("PASS: VSQRTPS C5/C4 WIG 128/256 all register roles, memory widths, reserved fields/maps, truncation and reset");return 0;
}
