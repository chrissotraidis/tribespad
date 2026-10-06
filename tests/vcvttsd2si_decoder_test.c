#include "udis86.h"
#include <assert.h>
#include <stdio.h>
static void invalid(const unsigned char *raw,unsigned n) {
    ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);
    assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);
}
static void positive(unsigned form,unsigned dest,unsigned source,unsigned memory) {
    unsigned char bytes[6];unsigned n=0;
    bytes[n++]=form?0xc4:0xc5;if(form)bytes[n++]=0xe1;
    bytes[n++]=form==1?0x7b:0xfb;bytes[n++]=0x2c;bytes[n++]=(memory?0:0xc0)|(dest<<3)|source;
    unsigned length=n;bytes[n++]=0xc3;
    ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_syntax(&u,UD_SYN_INTEL);ud_set_input_buffer(&u,bytes,n);
    assert(ud_disassemble(&u)==length&&u.mnemonic==UD_Ivcvttsd2si);
    assert(u.operand[0].type==UD_OP_REG&&u.operand[0].base==UD_R_EAX+dest&&u.operand[0].size==32);
    assert(u.operand[1].type==(memory?UD_OP_MEM:UD_OP_REG)&&u.operand[1].base==(memory?UD_R_ESI:UD_R_XMM0+source)&&u.operand[1].size==(memory?64:128));
    assert(u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE);
    assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&u.operand[1].type==UD_NONE&&u.operand[2].type==UD_NONE&&u.operand[3].type==UD_NONE&&!ud_disassemble(&u));
    for(unsigned i=1;i<length;i++)invalid(bytes,i);
}
int main(void) {
    for(unsigned f=0;f<3;f++)for(unsigned d=0;d<8;d++) {
        for(unsigned s=0;s<8;s++)positive(f,d,s,0);positive(f,d,6,1);
    }
    const unsigned char rejected[][5]={
        {0xc5,0xf3,0x2c,0xc0},{0xc5,0xff,0x2c,0xc0},
        {0xc5,0xf8,0x2c,0xc0},{0xc5,0xf9,0x2c,0xc0},{0xc5,0xfa,0x2c,0xc0},{0xc5,0xfb,0x2d,0xc0},
        {0xc4,0xe2,0x7b,0x2c,0xc0},{0xc4,0xe3,0x7b,0x2c,0xc0},{0xc4,0xc1,0x7b,0x2c,0xc0}
    };
    for(unsigned i=0;i<sizeof rejected/sizeof rejected[0];i++)invalid(rejected[i],i<6?4:5);
    invalid((const unsigned char *)"\xf0\xc5\xfb\x2c\xc0",5);
    invalid((const unsigned char *)"\x66\xc5\xfb\x2c\xc0",5);
    invalid((const unsigned char *)"\xf3\xc5\xfb\x2c\xc0",5);
    puts("PASS: VCVTTSD2SI C5/C4 IA-32 WIG L0, all GPR/XMM roles, memory width, finite unsupported fields/maps, truncation and reset");return 0;
}
