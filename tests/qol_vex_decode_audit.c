/* Host decoding inventory; no native execution or lowering claim. */
#include "udis86.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    unsigned pc;char hex[31];
    while(scanf("%x %30s",&pc,hex)==2) {
        unsigned n=(unsigned)strlen(hex)/2;unsigned char bytes[15];
        if(!n||n>15||strlen(hex)!=n*2)return 2;
        for(unsigned i=0;i<n;i++){unsigned v;if(sscanf(hex+i*2,"%2x",&v)!=1)return 2;bytes[i]=(unsigned char)v;}
        ud_t u;ud_init(&u);ud_set_mode(&u,32);ud_set_input_buffer(&u,bytes,n);ud_set_pc(&u,pc);
        unsigned length=ud_disassemble(&u);printf("%x %u %s\n",pc,length,ud_lookup_mnemonic(u.mnemonic));
    }
    return ferror(stdin)?1:0;
}
