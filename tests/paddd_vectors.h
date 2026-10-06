#include <stdint.h>
static void paddd_pattern(unsigned i,uint32_t left[4],uint32_t right[4]) {
    left[0]=i|(i<<16);right[0]=1;
    left[1]=(1u<<(i%32))-1;right[1]=1;
    left[2]=0x7fffffffu+i;right[2]=0x80000001u-i;
    uint32_t seed=0x34fe5u+i;seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;left[3]=seed;
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;right[3]=seed;
}
