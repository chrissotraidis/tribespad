/* Native x86 reference for the adjacent environment save/restore query. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>
int main(void) {
    int translated=0;size_t n=sizeof translated;
    assert(!sysctlbyname("sysctl.proc_translated",&translated,&n,0,0)&&translated);
    unsigned char state[108], before[28], saved[28], after[28];
    while(fread(state,sizeof state,1,stdin)==1) {
        __asm__ volatile("fninit; frstor %3; fnstenv %0; fldenv %0; fnstenv %1; fldenv %1; fnstenv %2; fninit"
            : "=m"(before), "=m"(saved), "=m"(after) : "m"(state) : "memory");
        assert(fwrite(before,28,1,stdout)==1);
        assert(fwrite(saved,28,1,stdout)==1);
        assert(fwrite(after,28,1,stdout)==1);
    }
}
