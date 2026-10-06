#include "../port/llasm-support/llasm_cpu.h"
#include "softfloat.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
typedef struct {uint32_t source,control,result,raised;} Case;
#include "cases.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
static uint32_t state=0x13b754ef;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
    unsigned long long count=0;unsigned values=(sizeof cases/sizeof cases[0])/64;
    for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<512;iteration++) {
        const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],input[32],result[32];unsigned char *memory=backing+17;
        for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
        for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
        unsigned control_index=iteration%64,raised=0;
        cpu._mxcsr=cases[control_index].control;cpu._pointer_offset=(uintptr_t)memory-0x200000;
        for(unsigned lane=0;lane<(c->wide?8:4);lane++) {
            unsigned value=(block+iteration*8+lane*587)%values;
            const Case *test=&cases[value*64+control_index];assert(test->control==cpu._mxcsr);
            memcpy(input+lane*4,&test->source,4);memcpy(result+lane*4,&test->result,4);raised|=test->raised;
        }
        unsigned offset=c->address==3?3:iteration%(65-(c->wide?32:16));
        if(c->kind) {
            memcpy(memory+offset,input,c->wide?32:16);
            if(c->address==0)cpu._eax=0x200000+offset;
            else if(c->address==1)cpu._esp=0x200000+offset-16;
            else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
        } else {
            memcpy(cpu._xmm[c->source],input,16);
            if(c->wide)memcpy(cpu._ymm_hi[c->source],input+16,16);
        }
        expected=cpu;memcpy(wanted,backing,sizeof wanted);
        memcpy(expected._xmm[c->dest],result,16);
        if(c->wide)memcpy(expected._ymm_hi[c->dest],result+16,16);else memset(expected._ymm_hi[c->dest],0,16);
        expected._mxcsr|=raised;
        softfloat_roundingMode=iteration%4;softfloat_detectTininess=iteration&1;
        softfloat_exceptionFlags=iteration&31;extF80_roundingPrecision=(uint_fast8_t[]){32,64,80}[iteration%3];
        unsigned round=softfloat_roundingMode,tiny=softfloat_detectTininess,flags=softfloat_exceptionFlags,precision=extF80_roundingPrecision;
        bodies[block](&cpu);
        assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));
        assert(round==softfloat_roundingMode&&tiny==softfloat_detectTininess&&flags==softfloat_exceptionFlags&&precision==extF80_roundingPrecision);count++;
    }
    printf("PASS: %llu strict emitted VSQRTPS cases; full CPU/guarded memory, all register aliases, 128/256 widths, all rounding/DAZ/FTZ controls, aggregated status and complete TLS restoration\n",count);return 0;
}
