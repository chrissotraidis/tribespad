#include "../port/llasm-support/llasm_cpu.h"
#include "softfloat.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(TRIBESPAD_TRUNC_HOST_FAULT_TESTS)
#include <signal.h>
#include <sys/wait.h>
#endif
#include "moves.inc"
typedef struct {uint64_t source;uint32_t control,result,raised;} Case;
#include "cases.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
_Static_assert(offsetof(_cpu,_eax)==0&&offsetof(_cpu,_ecx)==4&&offsetof(_cpu,_edx)==8&&offsetof(_cpu,_ebx)==12&&offsetof(_cpu,_esp)==16&&offsetof(_cpu,_ebp)==20&&offsetof(_cpu,_esi)==24&&offsetof(_cpu,_edi)==28,"IA-32 GPR order");
static uint32_t state=0x13b754ef;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
    unsigned long long count=0;unsigned candidates=sizeof cases/sizeof cases[0];
    for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<(block?2048:candidates);iteration++) {
        const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97];unsigned char *memory=backing+17;
        for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
        for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
        const Case *test=&cases[block?((block*2048+iteration)*17)%candidates:iteration];
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
    size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+page,page,PROT_NONE));unsigned protected_count=0;
    for(unsigned block=0;block<sizeof moves/sizeof moves[0];++block){const move_case *c=&moves[block];if(!c->kind)continue;for(unsigned i=0;i<128;++i){const Case *test=&cases[i*151%candidates];_cpu cpu,expected;for(unsigned j=0;j<sizeof cpu;++j)((unsigned char *)&cpu)[j]=(unsigned char)random_word();cpu._mxcsr=test->control;cpu._eax=0x1003;cpu._esp=0x1100;cpu._ebx=0x1400;cpu._esi=0x11;uint32_t address=c->address==0?cpu._eax:c->address==1?cpu._esp+64:c->address==2?cpu._ebx+cpu._esi*4-17:0x200003;cpu._pointer_offset=(uintptr_t)(guard+page-8)-address;memcpy(guard+page-8,&test->source,8);expected=cpu;memcpy((unsigned char *)&expected+c->dest*4,&test->result,4);expected._mxcsr|=test->raised;bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu));assert(!memcmp(guard+page-8,&test->source,8));++count;++protected_count;}}
    unsigned faults=0;
#if defined(TRIBESPAD_TRUNC_HOST_FAULT_TESTS)
    _cpu *shared=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(shared!=MAP_FAILED);
    for(unsigned which=0;which<2;++which){for(unsigned j=0;j<sizeof *shared;++j)((unsigned char *)shared)[j]=(unsigned char)random_word();unsigned raised=which?32:1;uint64_t input=which?UINT64_C(0x3ff8000000000000):UINT64_C(0x7ff0123456789abc);shared->_mxcsr=0x1f80&~(raised<<7);shared->_esp=0x1003;shared->_pointer_offset=(uintptr_t)&input-(shared->_esp+64);_cpu expected=*shared;expected._mxcsr|=raised;pid_t child=fork();assert(child>=0);if(!child){bodies[0](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(shared,&expected,sizeof expected));++faults;}
    shared->_pointer_offset=(uintptr_t)(guard+page)-(shared->_esp+64);_cpu before=*shared;pid_t child=fork();assert(child>=0);if(!child){bodies[0](shared);_exit(7);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));assert(!memcmp(shared,&before,sizeof before));++faults;assert(!munmap(shared,page));
#endif
    assert(!munmap(guard,page*2));
    printf("PASS: %llu emitted legacy CVTTSD2SI checks; %u protected8byte reads; %u host faults; full CPU/vector/flags/TLS preservation\n",count,protected_count,faults);return 0;
}
