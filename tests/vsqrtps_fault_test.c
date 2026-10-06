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
static void probe(unsigned wide,unsigned memory,unsigned category) {
    unsigned block=0,found=0;
    for(;block<sizeof moves/sizeof moves[0];block++) {
        const move_case *c=&moves[block];
        if(c->wide==wide&&c->kind==memory&&c->dest==0&&c->source==0&&c->address==0){found=1;break;}
    }
    assert(found);
    unsigned char *shared=mmap(NULL,sizeof(_cpu)+97,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
    assert(shared!=MAP_FAILED);memset(shared,0xa5,sizeof(_cpu)+97);_cpu *cpu=(_cpu *)shared;
    cpu->_mxcsr=(uint32_t[]){0x1f00,0x1f00,0x1e80,0x0f80,0x1f00}[category];
    uint32_t input[8];for(unsigned n=0;n<8;n++)input[n]=0x3f800000;
    input[wide?7:3]=(uint32_t[]){0xbf800000,0x7f800001,2,0x40000000,0xbf800000}[category];
    if(category==4){input[0]=2;input[1]=0x40000000;}
    memcpy(cpu->_xmm[0],input,16);memcpy(cpu->_ymm_hi[0],input+4,16);
    memcpy(shared+sizeof *cpu+17,input,wide?32:16);
    cpu->_pointer_offset=(uintptr_t)(shared+sizeof *cpu+17)-0x200000;cpu->_eax=0x200000;
    _cpu expected=*cpu;expected._mxcsr|=(unsigned[]){1,1,2,32,35}[category];
    unsigned char wanted[97];memcpy(wanted,shared+sizeof *cpu,97);
    pid_t child=fork();assert(child>=0);
    if(!child){bodies[block](cpu);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
    assert(!memcmp(cpu,&expected,sizeof *cpu)&&!memcmp(shared+sizeof *cpu,wanted,97));
    assert(!munmap(shared,sizeof(_cpu)+97));
}
int main(void) {
    for(unsigned wide=0;wide<2;wide++)for(unsigned memory=0;memory<2;memory++)for(unsigned category=0;category<5;category++)probe(wide,memory,category);
    puts("PASS: 20 emitted VSQRTPS unmasked/mixed late-lane probes preserve full destination/CPU and guarded memory while aggregating MXCSR; SIGABRT boundary, not Windows exception delivery");return 0;
}
