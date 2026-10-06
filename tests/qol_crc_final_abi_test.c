#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_zlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "cases.inc"
extern void tribes_enter(_cpu *, uint32_t);
static void check(unsigned char *arena,unsigned size,uint32_t source,uint32_t stack,const struct crc_case *v,uint64_t base) {
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=stack;
    unsigned char *state=(unsigned char *)(uintptr_t)(base+source);
    memcpy(state,v->fold,64);memcpy(state+64,&v->previous_value,4);
    unsigned char *arguments=(unsigned char *)(uintptr_t)(base+stack);memcpy(arguments,&source,4);
    unsigned char *expected=malloc(size);assert(expected);memcpy(expected,arena,size);
    memcpy(expected+(state-arena)+64,&v->result,4);
    uint32_t host_return=0xfffff000;memcpy(expected+(arguments-arena)-4,&host_return,4);
    _cpu wanted=cpu;wanted._eax=v->result;memset(wanted._ymm_hi,0,80);
    tribes_enter(&cpu,0x100c9ba0);
    assert(!memcmp(&cpu,&wanted,sizeof cpu));assert(!memcmp(arena,expected,size));free(expected);
}
int main(int argc,char **argv) {
    if(argc>1){_cpu cpu;memset(&cpu,0,sizeof cpu);cpu._pointer_offset=!strcmp(argv[1],"host-overflow")?UINT64_MAX:0;
        uint32_t address=!strcmp(argv[1],"null")?0:!strcmp(argv[1],"guest-overflow")?0xfffffff0:1;
        tribes_zlib_crc_fold_final_cpu(&cpu,address);return 3;}
    unsigned count=0;unsigned char arena[8192];
    for(unsigned alignment=0;alignment<16;alignment++)for(unsigned c=0;c<sizeof cases/sizeof cases[0];c++) {
        memset(arena,0xa6,sizeof arena);check(arena,sizeof arena,0x100+alignment,0x1900+alignment,&cases[c],(uintptr_t)arena);count++;
    }
    size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(guard!=MAP_FAILED&&page>1024);assert(!mprotect(guard+page,page,PROT_NONE));
    for(unsigned c=0;c<sizeof cases/sizeof cases[0];c++) {
        memset(guard,0xa6,page);check(guard,(unsigned)page,0x1000+(uint32_t)page-68,0x1080,&cases[c],(uintptr_t)guard-0x1000);count++;
    }
    assert(!munmap(guard,page*2));printf("PASS: %u original-PC CRC final cdecl calls, full CPU/arena/protected page\n",count);return 0;
}
