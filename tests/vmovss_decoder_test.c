/* Host translator decoder contract, independent of native guest helpers. */
#include "udis86.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check(const unsigned char *raw,unsigned n,enum ud_mnemonic_code mnemonic) {
    unsigned char stream[32];assert(n<sizeof stream);memcpy(stream,raw,n);stream[n]=0xc3;
    ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,stream,n+1);
    ud_set_pc(&u,0x1000);ud_set_syntax(&u,UD_SYN_INTEL);
    assert(ud_disassemble(&u)==n&&u.mnemonic==mnemonic);
    assert(ud_insn_off(&u)==0x1000&&!memcmp(ud_insn_ptr(&u),raw,n));
    if((mnemonic==UD_Ivmovss||mnemonic==UD_Ivmulss||mnemonic==UD_Ivdivss||mnemonic==UD_Ivsubss||mnemonic==UD_Ivaddss||mnemonic==UD_Ivcvtss2sd||mnemonic==UD_Ivxorpd||mnemonic==UD_Ivxorps||mnemonic==UD_Ivsqrtsd||mnemonic==UD_Ivcvtsd2ss)&&u.operand[2].type==UD_OP_REG) {
        assert(u.operand[0].base==UD_R_XMM0&&u.operand[1].base==UD_R_XMM1&&u.operand[2].base==UD_R_XMM2);
        for(unsigned i=0;i<3;i++)assert(u.operand[i].size==128);
    }
    if(mnemonic==UD_Ivmovss||mnemonic==UD_Ivmulss||mnemonic==UD_Ivdivss||mnemonic==UD_Ivsubss||mnemonic==UD_Ivaddss||mnemonic==UD_Ivcvtss2sd||mnemonic==UD_Ivxorpd||mnemonic==UD_Ivxorps||mnemonic==UD_Ivsqrtsd||mnemonic==UD_Ivcvtsd2ss)for(unsigned i=0;i<3;i++)if(u.operand[i].type==UD_OP_MEM) {
        assert(u.operand[i].size==((mnemonic==UD_Ivxorpd||mnemonic==UD_Ivxorps)?128:(mnemonic==UD_Ivsqrtsd||mnemonic==UD_Ivcvtsd2ss)?64:32)&&u.operand[i].base==UD_R_ESI);
    }
    if(mnemonic==UD_Ivucomisd) {
        assert(u.operand[0].base==UD_R_XMM1&&u.operand[0].size==128&&u.operand[2].type==UD_NONE);
        if(u.operand[1].type==UD_OP_REG)assert(u.operand[1].base==UD_R_XMM2&&u.operand[1].size==128);
        else assert(u.operand[1].type==UD_OP_MEM&&u.operand[1].size==64&&u.operand[1].base==UD_R_ESI);
    }
    if(mnemonic==UD_Ivcomiss) {
        assert(u.operand[0].base==UD_R_XMM1&&u.operand[0].size==128&&u.operand[2].type==UD_NONE);
        if(u.operand[1].type==UD_OP_REG)assert(u.operand[1].base==UD_R_XMM2&&u.operand[1].size==128);
        else assert(u.operand[1].type==UD_OP_MEM&&u.operand[1].size==32&&u.operand[1].base==UD_R_ESI);
    }
    if(mnemonic==UD_Ivmulps||mnemonic==UD_Ivsubps||mnemonic==UD_Ivaddps||mnemonic==UD_Ivdivps) {
        unsigned wide=(raw[raw[0]==0xc4?2:1]&4)!=0,base=wide?UD_R_YMM0:UD_R_XMM0;
        assert(u.operand[0].base==base&&u.operand[1].base==base+1);
        for(unsigned i=0;i<3;i++)assert(u.operand[i].size==(wide?256:128));
        if(u.operand[2].type==UD_OP_REG)assert(u.operand[2].base==base+2);
        else assert(u.operand[2].type==UD_OP_MEM&&u.operand[2].base==UD_R_ESI);
    }
    if(mnemonic==UD_Ivmovhlps||mnemonic==UD_Ivmovlhps||mnemonic==UD_Ivmovlps||mnemonic==UD_Ivmovhps) {
        if(u.operand[0].type==UD_OP_MEM) {
            assert(u.operand[0].size==64&&u.operand[0].base==UD_R_ESI&&u.operand[1].base==UD_R_XMM2&&u.operand[1].size==128&&u.operand[2].type==UD_NONE);
        } else {
            assert(u.operand[0].base==UD_R_XMM0&&u.operand[1].base==UD_R_XMM1);
            assert(u.operand[0].size==128&&u.operand[1].size==128);
            if(u.operand[2].type==UD_OP_MEM)assert(u.operand[2].size==64&&u.operand[2].base==UD_R_ESI);
            else assert(u.operand[2].base==UD_R_XMM2&&u.operand[2].size==128);
        }
    }
    if(mnemonic==UD_Ivextractf128) {
        assert(u.operand[1].type==UD_OP_REG&&u.operand[1].base==UD_R_YMM2&&u.operand[1].size==256);
        assert(u.operand[0].size==128);
        if(u.operand[0].type==UD_OP_REG)assert(u.operand[0].base==UD_R_XMM0);
        else assert(u.operand[0].type==UD_OP_MEM&&u.operand[0].base==UD_R_ESI);
        assert(u.operand[2].type==UD_OP_IMM&&u.operand[2].size==8&&u.operand[2].lval.ubyte==raw[n-1]);
    }
    if(mnemonic==UD_Ivshufps) {
        unsigned wide=(raw[raw[0]==0xc4?2:1]&4)!=0,base=wide?UD_R_YMM0:UD_R_XMM0;
        assert(u.operand[0].base==base&&u.operand[1].base==base+1);
        for(unsigned i=0;i<3;i++)assert(u.operand[i].size==(wide?256:128));
        if(u.operand[2].type==UD_OP_REG)assert(u.operand[2].base==base+2);
        else assert(u.operand[2].type==UD_OP_MEM&&u.operand[2].base==UD_R_ESI);
        assert(u.operand[3].type==UD_OP_IMM&&u.operand[3].size==8&&u.operand[3].lval.ubyte==raw[n-1]);
        assert(ud_insn_opr(&u,3)==&u.operand[3]&&!ud_insn_opr(&u,4));
    } else assert(u.operand[3].type==UD_NONE);
    if(mnemonic==UD_Ivzeroupper)for(unsigned i=0;i<3;i++)assert(u.operand[i].type==UD_NONE);
    if(mnemonic==UD_Ivmovaps||mnemonic==UD_Ivmovups||mnemonic==UD_Ivmovdqa||mnemonic==UD_Ivmovdqu) {
        assert(u.operand[2].type==UD_NONE);
        unsigned wide=(raw[n==5?2:1]&4)!=0;unsigned base=wide?UD_R_YMM0:UD_R_XMM0;
        for(unsigned i=0;i<2;i++)assert(u.operand[i].size==(wide?256:128));
        if(u.operand[0].type==UD_OP_MEM)assert(u.operand[0].base==UD_R_ESI&&u.operand[1].base==base+2);
        else if(u.operand[1].type==UD_OP_MEM)assert(u.operand[0].base==base&&u.operand[1].base==UD_R_ESI);
        else assert(u.operand[0].base==base&&u.operand[1].base==base+2);
    }
    assert(ud_disassemble(&u)==1&&u.mnemonic==UD_Iret&&ud_insn_off(&u)==0x1000+n);
    assert(u.operand[3].type==UD_NONE&&!ud_insn_opr(&u,3));
    assert(!ud_disassemble(&u));
}
static void invalid(const unsigned char *raw,unsigned n) {
    ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,raw,n);
    assert(ud_disassemble(&u)>0&&u.mnemonic==UD_Iinvalid);
}
static void third_memory_segment(void) {
    static const unsigned char inputs[][6]={
        {0x64,0xc5,0xf2,0x59,0x06},
        {0x64,0xc5,0xf2,0x58,0x06},
        {0x64,0xc5,0xf2,0x5c,0x06},
        {0x64,0xc5,0xf2,0x5e,0x06},
        {0x64,0xc5,0xf2,0x5a,0x06},
        {0x64,0xc5,0xf3,0x5a,0x06},
        {0x64,0xc5,0xf3,0x51,0x06},
        {0x64,0xc5,0xf1,0x57,0x06},
        {0x64,0xc5,0xf0,0x57,0x06},
        {0x64,0xc5,0xf0,0xc6,0x06,0x99},
    };
    for(unsigned i=0;i<sizeof inputs/sizeof inputs[0];i++) {
        ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,inputs[i],i==9?6:5);ud_set_syntax(&u,UD_SYN_INTEL);
        assert(ud_disassemble(&u)==(i==9?6:5)&&u.mnemonic!=UD_Iinvalid);
        assert(u.pfx_seg==UD_R_FS&&u.operand[2].type==UD_OP_MEM);
        assert(strstr(ud_insn_asm(&u),"[fs:esi]"));
    }
}
int main(void) {
    third_memory_segment();
    for(unsigned form=0;form<3;form++)for(unsigned wide=0;wide<2;wide++)for(unsigned sub=0;sub<4;sub++) {
        unsigned char bytes[]={0xc4,0xe1,0x70|(wide?4:0)|(form==2?0x80:0),sub==3?0x5e:sub==2?0x58:sub?0x5c:0x59,0xc2};
        if(form==0){bytes[1]=0xc5;bytes[2]|=0x80;}
        check(bytes+(form==0),form==0?4:5,sub==3?UD_Ivdivps:sub==2?UD_Ivaddps:sub?UD_Ivsubps:UD_Ivmulps);
        bytes[4]=0x06;check(bytes+(form==0),form==0?4:5,sub==3?UD_Ivdivps:sub==2?UD_Ivaddps:sub?UD_Ivsubps:UD_Ivmulps);
    }
    for(unsigned form=0;form<3;form++)for(unsigned high=0;high<2;high++) {
        unsigned char bytes[]={0xc4,0xe1,0x70|(form==2?0x80:0),high?0x16:0x12,0xc2};
        unsigned char mem[]={0xc4,0xe1,0x70|(form==2?0x80:0),high?0x16:0x12,0x06};
        unsigned char store[]={0xc4,0xe1,0x78|(form==2?0x80:0),high?0x17:0x13,0x16};
        if(form==0) {bytes[1]=0xc5;bytes[2]|=0x80;mem[1]=0xc5;mem[2]|=0x80;store[1]=0xc5;store[2]|=0x80;}
        check(bytes+(form==0),form==0?4:5,high?UD_Ivmovlhps:UD_Ivmovhlps);
        check(mem+(form==0),form==0?4:5,high?UD_Ivmovhps:UD_Ivmovlps);
        check(store+(form==0),form==0?4:5,high?UD_Ivmovhps:UD_Ivmovlps);
    }
    invalid((const unsigned char *)"\xc5\xfc\x16\xc2",4);
    invalid((const unsigned char *)"\xc5\xf0\x13\x16",4);
    invalid((const unsigned char *)"\xc5\xf8\x17\xc2",4);
    for(unsigned control=0;control<256;control++) {
        unsigned char reg[]={0xc4,0xe3,0x7d,0x19,0xd0,control};
        unsigned char mem[]={0xc4,0xe3,0x7d,0x19,0x16,control};
        check(reg,6,UD_Ivextractf128);check(mem,6,UD_Ivextractf128);
        invalid(reg,5);invalid(mem,5);
    }
    invalid((const unsigned char *)"\xc4\xe3\x79\x19\xd0\x01",6);
    invalid((const unsigned char *)"\xc4\xe3\xfd\x19\xd0\x01",6);
    invalid((const unsigned char *)"\xc4\xe3\x75\x19\xd0\x01",6);
    invalid((const unsigned char *)"\xc4\xe3\x7c\x19\xd0\x01",6);
    invalid((const unsigned char *)"\xc4\xe3\x7e\x19\xd0\x01",6);
    invalid((const unsigned char *)"\xc4\xe3\x7f\x19\xd0\x01",6);
    check((const unsigned char *)"\xc5\xf2\x10\xc2",4,UD_Ivmovss);
    check((const unsigned char *)"\xc5\xf2\x11\xd0",4,UD_Ivmovss);
    check((const unsigned char *)"\xc4\xe1\x72\x10\xc2",5,UD_Ivmovss);
    check((const unsigned char *)"\xc4\xe1\xf2\x10\xc2",5,UD_Ivmovss);
    check((const unsigned char *)"\xc5\xfa\x10\x46\x04",5,UD_Ivmovss);
    check((const unsigned char *)"\xc5\xfa\x11\x0e",4,UD_Ivmovss);
    check((const unsigned char *)"\xc5\xf2\x59\xc2",4,UD_Ivmulss);
    check((const unsigned char *)"\xc5\xf2\x58\xc2",4,UD_Ivaddss);
    check((const unsigned char *)"\xc5\xf2\x59\x06",4,UD_Ivmulss);
    check((const unsigned char *)"\xc5\xf2\x58\x06",4,UD_Ivaddss);
    check((const unsigned char *)"\xc5\xf2\x5a\xc2",4,UD_Ivcvtss2sd);
    check((const unsigned char *)"\xc5\xf2\x5a\x06",4,UD_Ivcvtss2sd);
    check((const unsigned char *)"\xc5\xf1\x57\xc2",4,UD_Ivxorpd);
    check((const unsigned char *)"\xc5\xf1\x57\x06",4,UD_Ivxorpd);
    check((const unsigned char *)"\xc4\xe1\xf1\x57\xc2",5,UD_Ivxorpd);
    check((const unsigned char *)"\xc5\xf9\x2e\xca",4,UD_Ivucomisd);
    check((const unsigned char *)"\xc5\xf9\x2e\x0e",4,UD_Ivucomisd);
    check((const unsigned char *)"\xc4\xe1\x79\x2e\xca",5,UD_Ivucomisd);
    check((const unsigned char *)"\xc4\xe1\xf9\x2e\xca",5,UD_Ivucomisd);
    invalid((const unsigned char *)"\xc5\xf1\x2e\xca",4);
    invalid((const unsigned char *)"\xc5\xf1\x2e\x0e",4);
    check((const unsigned char *)"\xc5\xf3\x51\xc2",4,UD_Ivsqrtsd);
    check((const unsigned char *)"\xc5\xf3\x51\x06",4,UD_Ivsqrtsd);
    check((const unsigned char *)"\xc4\xe1\x73\x51\xc2",5,UD_Ivsqrtsd);
    check((const unsigned char *)"\xc4\xe1\xf3\x51\xc2",5,UD_Ivsqrtsd);
    invalid((const unsigned char *)"\xc5\xf7\x51\xc2",4);
    check((const unsigned char *)"\xc5\xf3\x5a\xc2",4,UD_Ivcvtsd2ss);
    check((const unsigned char *)"\xc5\xf3\x5a\x06",4,UD_Ivcvtsd2ss);
    check((const unsigned char *)"\xc4\xe1\x73\x5a\xc2",5,UD_Ivcvtsd2ss);
    check((const unsigned char *)"\xc4\xe1\xf3\x5a\xc2",5,UD_Ivcvtsd2ss);
    invalid((const unsigned char *)"\xc5\xf7\x5a\xc2",4);
    invalid((const unsigned char *)"\xc5\xf1\x5a\xc2",4);
    check((const unsigned char *)"\xc5\xf8\x2f\xca",4,UD_Ivcomiss);
    check((const unsigned char *)"\xc5\xf8\x2f\x0e",4,UD_Ivcomiss);
    check((const unsigned char *)"\xc4\xe1\x78\x2f\xca",5,UD_Ivcomiss);
    check((const unsigned char *)"\xc4\xe1\xf8\x2f\xca",5,UD_Ivcomiss);
    invalid((const unsigned char *)"\xc5\xf0\x2f\xca",4);
    invalid((const unsigned char *)"\xc5\xf0\x2f\x0e",4);
    check((const unsigned char *)"\xc5\xf0\x57\xc2",4,UD_Ivxorps);
    check((const unsigned char *)"\xc5\xf0\x57\x06",4,UD_Ivxorps);
    check((const unsigned char *)"\xc4\xe1\x70\x57\xc2",5,UD_Ivxorps);
    check((const unsigned char *)"\xc4\xe1\xf0\x57\xc2",5,UD_Ivxorps);
    invalid((const unsigned char *)"\xc5\xf4\x57\xc2",4);
    invalid((const unsigned char *)"\xc5\xf2\x57\xc2",4);
    check((const unsigned char *)"\xc5\xf2\x5e\xc2",4,UD_Ivdivss);
    check((const unsigned char *)"\xc5\xf2\x5e\x06",4,UD_Ivdivss);
    check((const unsigned char *)"\xc4\xe1\x72\x5e\xc2",5,UD_Ivdivss);
    check((const unsigned char *)"\xc4\xe1\xf2\x5e\xc2",5,UD_Ivdivss);
    invalid((const unsigned char *)"\xc5\xf6\x5e\xc2",4);
    check((const unsigned char *)"\xc5\xf8\x28\xc2",4,UD_Ivmovaps);
    check((const unsigned char *)"\xc5\xf8\x29\xd0",4,UD_Ivmovaps);
    check((const unsigned char *)"\xc5\xf8\x28\x06",4,UD_Ivmovaps);
    check((const unsigned char *)"\xc5\xf8\x29\x16",4,UD_Ivmovaps);
    check((const unsigned char *)"\xc4\xe1\x78\x28\xc2",5,UD_Ivmovaps);
    check((const unsigned char *)"\xc4\xe1\xf8\x28\xc2",5,UD_Ivmovaps);
    invalid((const unsigned char *)"\xc5\xf0\x28\xc2",4);
    invalid((const unsigned char *)"\xc5\xf0\x28\x06",4);
    invalid((const unsigned char *)"\xc5\xf0\x29\x16",4);
    check((const unsigned char *)"\xc5\xfc\x28\xc2",4,UD_Ivmovaps);
    check((const unsigned char *)"\xc5\xfc\x29\xd0",4,UD_Ivmovaps);
    check((const unsigned char *)"\xc5\xfc\x28\x06",4,UD_Ivmovaps);
    check((const unsigned char *)"\xc5\xfc\x29\x16",4,UD_Ivmovaps);
    check((const unsigned char *)"\xc4\xe1\x7c\x28\xc2",5,UD_Ivmovaps);
    check((const unsigned char *)"\xc4\xe1\xfc\x28\xc2",5,UD_Ivmovaps);
    invalid((const unsigned char *)"\xc5\xf4\x28\xc2",4);
    invalid((const unsigned char *)"\xc5\xf4\x28\x06",4);
    invalid((const unsigned char *)"\xc5\xf4\x29\x16",4);
    check((const unsigned char *)"\xc5\xf8\x10\xc2",4,UD_Ivmovups);
    check((const unsigned char *)"\xc5\xf8\x11\xd0",4,UD_Ivmovups);
    check((const unsigned char *)"\xc5\xf8\x10\x06",4,UD_Ivmovups);
    check((const unsigned char *)"\xc5\xf8\x11\x16",4,UD_Ivmovups);
    check((const unsigned char *)"\xc5\xfc\x10\xc2",4,UD_Ivmovups);
    check((const unsigned char *)"\xc5\xfc\x11\xd0",4,UD_Ivmovups);
    check((const unsigned char *)"\xc5\xfc\x10\x06",4,UD_Ivmovups);
    check((const unsigned char *)"\xc5\xfc\x11\x16",4,UD_Ivmovups);
    check((const unsigned char *)"\xc4\xe1\x7c\x10\xc2",5,UD_Ivmovups);
    check((const unsigned char *)"\xc4\xe1\xfc\x10\xc2",5,UD_Ivmovups);
    invalid((const unsigned char *)"\xc5\xf4\x10\xc2",4);
    invalid((const unsigned char *)"\xc5\xf4\x10\x06",4);
    invalid((const unsigned char *)"\xc5\xf4\x11\x16",4);
    check((const unsigned char *)"\xc5\xf8\x77",3,UD_Ivzeroupper);
    check((const unsigned char *)"\x64\xc5\xf8\x77",4,UD_Ivzeroupper);
    check((const unsigned char *)"\xc4\xe1\x78\x77",4,UD_Ivzeroupper);
    check((const unsigned char *)"\xc4\xe1\xf8\x77",4,UD_Ivzeroupper);
    invalid((const unsigned char *)"\xc5\xfc\x77",3);
    invalid((const unsigned char *)"\xc5\xf0\x77",3);
    invalid((const unsigned char *)"\xc5\xf9\x77",3);
    invalid((const unsigned char *)"\x66\xc5\xf8\x77",4);
    check((const unsigned char *)"\xc5\xf9\x6f\xc2",4,UD_Ivmovdqa);
    check((const unsigned char *)"\xc5\xf9\x6f\x06",4,UD_Ivmovdqa);
    check((const unsigned char *)"\xc5\xf9\x7f\xd0",4,UD_Ivmovdqa);
    check((const unsigned char *)"\xc5\xf9\x7f\x16",4,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\x79\x6f\xc2",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\x79\x6f\x06",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\x79\x7f\xd0",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\x79\x7f\x16",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\xf9\x6f\xc2",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\xf9\x6f\x06",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\xf9\x7f\xd0",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\xf9\x7f\x16",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc5\xfd\x6f\xc2",4,UD_Ivmovdqa);
    check((const unsigned char *)"\xc5\xfd\x6f\x06",4,UD_Ivmovdqa);
    check((const unsigned char *)"\xc5\xfd\x7f\xd0",4,UD_Ivmovdqa);
    check((const unsigned char *)"\xc5\xfd\x7f\x16",4,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\x7d\x6f\xc2",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\x7d\x6f\x06",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\x7d\x7f\xd0",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\x7d\x7f\x16",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\xfd\x6f\xc2",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\xfd\x6f\x06",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\xfd\x7f\xd0",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc4\xe1\xfd\x7f\x16",5,UD_Ivmovdqa);
    check((const unsigned char *)"\xc5\xfa\x6f\xc2",4,UD_Ivmovdqu);
    check((const unsigned char *)"\xc5\xfa\x6f\x06",4,UD_Ivmovdqu);
    check((const unsigned char *)"\xc5\xfa\x7f\xd0",4,UD_Ivmovdqu);
    check((const unsigned char *)"\xc5\xfa\x7f\x16",4,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\x7a\x6f\xc2",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\x7a\x6f\x06",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\x7a\x7f\xd0",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\x7a\x7f\x16",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\xfa\x6f\xc2",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\xfa\x6f\x06",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\xfa\x7f\xd0",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\xfa\x7f\x16",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc5\xfe\x6f\xc2",4,UD_Ivmovdqu);
    check((const unsigned char *)"\xc5\xfe\x6f\x06",4,UD_Ivmovdqu);
    check((const unsigned char *)"\xc5\xfe\x7f\xd0",4,UD_Ivmovdqu);
    check((const unsigned char *)"\xc5\xfe\x7f\x16",4,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\x7e\x6f\xc2",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\x7e\x6f\x06",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\x7e\x7f\xd0",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\x7e\x7f\x16",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\xfe\x6f\xc2",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\xfe\x6f\x06",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\xfe\x7f\xd0",5,UD_Ivmovdqu);
    check((const unsigned char *)"\xc4\xe1\xfe\x7f\x16",5,UD_Ivmovdqu);
    invalid((const unsigned char *)"\xc5\xf1\x6f\xc2",4);
    invalid((const unsigned char *)"\xc5\xf5\x6f\x06",4);
    invalid((const unsigned char *)"\xc5\xf1\x7f\x16",4);
    invalid((const unsigned char *)"\xc5\xf2\x6f\xc2",4);
    invalid((const unsigned char *)"\xc5\xf6\x6f\x06",4);
    invalid((const unsigned char *)"\xc5\xf2\x7f\x16",4);
    invalid((const unsigned char *)"\xc5\xf8\x6f\xc2",4);
    invalid((const unsigned char *)"\xc5\xfb\x6f\xc2",4);
    check((const unsigned char *)"\xc5\xf0\xc6\xc2\x00",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf0\xc6\xc2\x55",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf0\xc6\xc2\xaa",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf0\xc6\xc2\xff",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf0\xc6\xc2\x99",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf0\xc6\x06\x00",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf0\xc6\x06\x55",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf0\xc6\x06\xaa",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf0\xc6\x06\xff",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf0\xc6\x06\x99",5,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\xc2\x00",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\xc2\x55",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\xc2\xaa",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\xc2\xff",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\xc2\x99",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\x06\x00",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\x06\x55",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\x06\xaa",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\x06\xff",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x70\xc6\x06\x99",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\xc2\x00",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\xc2\x55",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\xc2\xaa",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\xc2\xff",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\xc2\x99",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\x06\x00",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\x06\x55",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\x06\xaa",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\x06\xff",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf0\xc6\x06\x99",6,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\xc2\x00",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\xc2\x55",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\xc2\xaa",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\xc2\xff",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\xc2\x99",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\x06\x00",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\x06\x55",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\x06\xaa",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\x06\xff",5,UD_Ivshufps);
    check((const unsigned char *)"\xc5\xf4\xc6\x06\x99",5,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\xc2\x00",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\xc2\x55",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\xc2\xaa",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\xc2\xff",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\xc2\x99",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\x06\x00",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\x06\x55",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\x06\xaa",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\x06\xff",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\x74\xc6\x06\x99",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\xc2\x00",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\xc2\x55",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\xc2\xaa",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\xc2\xff",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\xc2\x99",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\x06\x00",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\x06\x55",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\x06\xaa",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\x06\xff",6,UD_Ivshufps);
    check((const unsigned char *)"\xc4\xe1\xf4\xc6\x06\x99",6,UD_Ivshufps);
    invalid((const unsigned char *)"\xc5\xf0\xc6\xc2",4);
    invalid((const unsigned char *)"\xc5\xf1\xc6\xc2\x99",5);
    check((const unsigned char *)"\xc5\xf2\x5c\xc2",4,UD_Ivsubss);
    check((const unsigned char *)"\xc5\xf2\x5c\x06",4,UD_Ivsubss);
    check((const unsigned char *)"\xc4\xe1\x72\x5c\xc2",5,UD_Ivsubss);
    check((const unsigned char *)"\xc4\xe1\xf2\x5c\xc2",5,UD_Ivsubss);
    invalid((const unsigned char *)"\xc5\xf6\x5c\xc2",4);
    check((const unsigned char *)"\xc5\x00",2,UD_Ilds);
    check((const unsigned char *)"\xc4\x00",2,UD_Iles);
    const unsigned char c5[]={0xc5,0xfa,0x10},c4[]={0xc4,0xe1,0x7a,0x10};
    for(unsigned n=1;n<=sizeof c5;n++)invalid(c5,n);
    for(unsigned n=1;n<=sizeof c4;n++)invalid(c4,n);
    invalid((const unsigned char *)"\xc5\xf2\x10\x00",4);
    invalid((const unsigned char *)"\xf3\xc5\xfa\x10\x00",5);
    puts("PASS: VMOVSS original-byte lengths/roles, WIG, sequential decoder reset, truncated input, reserved prefix and legacy LDS/LES distinction");
    return 0;
}
