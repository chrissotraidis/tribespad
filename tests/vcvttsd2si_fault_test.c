#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
static void probe(unsigned dest,unsigned memory,unsigned category) {
    unsigned block=0,found=0;
    for(;block<sizeof moves/sizeof moves[0];block++) {
        const move_case *c=&moves[block];
        if(c->kind==memory&&c->dest==dest&&(memory?c->address==1:c->source==2)){found=1;break;}
    }
    assert(found);
    unsigned char *shared=mmap(NULL,sizeof(_cpu)+97,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
    assert(shared!=MAP_FAILED);memset(shared,0xa5,sizeof(_cpu)+97);_cpu *cpu=(_cpu *)shared;
    uint64_t input=(uint64_t[]){UINT64_C(0x41e0000000000000),UINT64_C(0x7ff0000000000001),UINT64_C(0x3ff8000000000000),1,UINT64_C(0x7ff8000000000001),UINT64_C(0x3ff8000000000000),1,UINT64_C(0x4045000000000000)}[category];
    cpu->_mxcsr=(unsigned[]){0x1f00,0x1f00,0x0f80,0x0f80,0x0f80,0x1f00,0x7f,0x3f}[category];
    unsigned raised=(unsigned[]){1,1,32,32,1,32,0,0}[category];
    unsigned result=(unsigned[]){0x80000000,0x80000000,1,0,0x80000000,1,0,42}[category];
    memcpy(cpu->_xmm[2],&input,8);memcpy(shared+sizeof *cpu+17,&input,8);
    cpu->_pointer_offset=(uintptr_t)(shared+sizeof *cpu+17)-0x200000;cpu->_esp=0x200000-64;
    _cpu expected=*cpu;expected._mxcsr|=raised;
    if(category>=4)memcpy((unsigned char *)&expected+dest*4,&result,4);
    unsigned char wanted[97];memcpy(wanted,shared+sizeof *cpu,97);
    pid_t child=fork();assert(child>=0);
    if(!child){bodies[block](cpu);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child);
    if(category<4)assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
    else assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    assert(!memcmp(cpu,&expected,sizeof *cpu)&&!memcmp(shared+sizeof *cpu,wanted,97));
    assert(!munmap(shared,sizeof(_cpu)+97));
}
int main(void) {
    for(unsigned dest=0;dest<8;dest++)for(unsigned memory=0;memory<2;memory++)for(unsigned category=0;category<8;category++)probe(dest,memory,category);
    puts("PASS: 128 emitted VCVTTSD2SI probes: unmasked IE/PE preserve full CPU/guarded memory; masked IE with PE unmasked, masked PE with IE unmasked, DAZ and exact conversion ignore prior sticky bits; SIGABRT boundary, not Windows fault delivery");return 0;
}
