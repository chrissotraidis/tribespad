#include "../port/llasm-support/llasm_cpu.h"
#include "softfloat.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
typedef struct {uint64_t source;uint32_t control,result,raised;} Case;
#include "cases.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
_Static_assert(offsetof(_cpu,_eax)==0&&offsetof(_cpu,_ecx)==4&&offsetof(_cpu,_edx)==8&&offsetof(_cpu,_ebx)==12&&offsetof(_cpu,_esp)==16&&offsetof(_cpu,_ebp)==20&&offsetof(_cpu,_esi)==24&&offsetof(_cpu,_edi)==28,"IA-32 GPR order");
static uint32_t state=0x13b754ef;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
    unsigned long long count=0;unsigned candidates=sizeof cases/sizeof cases[0];
    for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<2048;iteration++) {
        const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97];unsigned char *memory=backing+17;
        for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
        for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
        const Case *test=&cases[((block*2048+iteration)*17)%candidates];
        cpu._mxcsr=test->control;cpu._pointer_offset=(uintptr_t)memory-0x200000;
        unsigned offset=c->address==3?3:iteration%57;
        if(c->kind) {
            memcpy(memory+offset,&test->source,8);
            if(c->address==0)cpu._eax=0x200000+offset;
            else if(c->address==1)cpu._esp=0x200000+offset-64;
            else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
        } else memcpy(cpu._xmm[c->source],&test->source,8);
        expected=cpu;memcpy(wanted,backing,sizeof wanted);
        memcpy((unsigned char *)&expected+c->dest*4,&test->result,4);expected._mxcsr|=test->raised;
        softfloat_roundingMode=iteration%4;softfloat_detectTininess=iteration&1;
        softfloat_exceptionFlags=iteration&31;extF80_roundingPrecision=(uint_fast8_t[]){32,64,80}[iteration%3];
        unsigned round=softfloat_roundingMode,tiny=softfloat_detectTininess,flags=softfloat_exceptionFlags,precision=extF80_roundingPrecision;
        bodies[block](&cpu);
        assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));
        assert(round==softfloat_roundingMode&&tiny==softfloat_detectTininess&&flags==softfloat_exceptionFlags&&precision==extF80_roundingPrecision);count++;
    }
    printf("PASS: %llu strict emitted VCVTTSD2SI cases; all GPR/XMM roles, full CPU/vector/flags/guarded memory preservation, truncation bounds/DAZ/status and TLS preservation\n",count);return 0;
}
