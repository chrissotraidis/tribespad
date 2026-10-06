/* Offline x86 oracle. Raw 80-bit output avoids host long-double conversion. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    (void)argv;
    unsigned op,cw,ea,eb; unsigned long long ma,mb;
    while(scanf("%u %x %llx %x %llx %x",&op,&cw,&ma,&ea,&mb,&eb)==6) {
        unsigned char a[10], b[10], z[10]; uint16_t control=cw,sw;
        uint64_t aa=ma,bb=mb,m; uint16_t ae=ea,be=eb,e;
        memcpy(a,&aa,8); memcpy(a+8,&ae,2);
        memcpy(b,&bb,8); memcpy(b+8,&be,2);
        __asm__ volatile("fninit; fldcw %0"::"m"(control):"st");
        if(op==4) __asm__ volatile("fldt %0; fsqrt"::"m"(a):"st");
        else {
            __asm__ volatile("fldt %0; fldt %1"::"m"(a),"m"(b):"st");
            switch(op) {
                case 0: __asm__ volatile("faddp":::"st");break;
                case 1: __asm__ volatile("fsubrp":::"st");break;
                case 2: __asm__ volatile("fmulp":::"st");break;
                default: __asm__ volatile("fdivrp":::"st");break;
            }
        }
        __asm__ volatile("fnstsw %0; fstpt %1; fninit":"=m"(sw),"=m"(z)::"st");
        memcpy(&m,z,8);memcpy(&e,z+8,2);
        printf("%016llx %04x %04x\n",(unsigned long long)m,e,sw&(argc>1?0x23f:63));
    }
}
