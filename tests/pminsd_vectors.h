#include <stdint.h>
static void pminsd_pattern(unsigned i,uint32_t left[4],uint32_t right[4]) {
    left[0]=i<32768?i:0xffff0000u|i;right[0]=0;
    left[1]=0x7fffffff;right[1]=0x80000000u+i;
    left[2]=0x80000000u+i;right[2]=0x7fffffffu-i;
    uint32_t seed=0x34fe5u+i;seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;left[3]=seed;
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;right[3]=seed;
}
