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
static void data_fill(unsigned char *p,const struct bulk_case *v,uint32_t count) {
    for(uint32_t j=0;j<count;++j) p[j]=v->pattern==0?0:v->pattern==1?255:(unsigned char)(j*17+v->length*3);
}
int main(int argc,char **argv) {
    if(argc>1) {
        unsigned char arena[1024];_cpu cpu;memset(&cpu,0,sizeof cpu);cpu._pointer_offset=(uintptr_t)arena;
        uint32_t source=128,length=256,lane0=16;
        if(!strcmp(argv[1],"short"))length=255;
        if(!strcmp(argv[1],"null"))source=0;
        if(!strcmp(argv[1],"state-null"))lane0=0;
        if(!strcmp(argv[1],"guest-overflow"))source=0xffffff80;
        if(!strcmp(argv[1],"host-overflow"))cpu._pointer_offset=UINT64_MAX;
        tribes_zlib_crc_fold_bulk_cpu(&cpu,lane0,32,48,64,source,length,0);return 3;
    }
    enum{SIZE=0x40000};unsigned char *arena=malloc(SIZE),*expected=malloc(SIZE);assert(arena&&expected);
    unsigned total=0;
    for(unsigned alignment=0;alignment<16;++alignment)for(unsigned c=0;c<sizeof cases/sizeof cases[0];++c) {
        const struct bulk_case *v=&cases[c];uint32_t lanes[4]={0x501+alignment,0x532+alignment,0x563+alignment,0x594+alignment},src=0x1010+alignment;
        memset(arena,0xa6,SIZE);data_fill(arena+src,v,v->length);
        for(unsigned lane=0;lane<4;++lane)memcpy(arena+lanes[lane],v->state+16*lane,16);
        _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)arena;cpu._esp=0x3a010+alignment;cpu._mxcsr=0xffffu-c;memcpy(cpu._xmm[0],v->seed,16);
        uint32_t args[7]={lanes[0],lanes[1],lanes[2],lanes[3],src,v->length,v->first};memcpy(arena+cpu._esp,args,sizeof args);memcpy(expected,arena,SIZE);
        uint32_t host_return=0xfffff000;memcpy(expected+cpu._esp-4,&host_return,4);
        for(unsigned lane=0;lane<4;++lane)memcpy(expected+lanes[lane],v->result+16*lane,16);
        _cpu wanted=cpu;wanted._eax=v->processed;memset(wanted._ymm_hi,0,sizeof wanted._ymm_hi);
        tribes_enter(&cpu,0x100c9e00);assert(!memcmp(&cpu,&wanted,sizeof cpu));assert(!memcmp(arena,expected,SIZE));++total;
    }
    /* Source ends after the processed prefix, even when length includes an
       unread remainder. Each separate16-byte state also ends at a guard. */
    size_t page=(size_t)sysconf(_SC_PAGESIZE),span=((65536+page-1)/page)*page;
    size_t size=span+page+8*page+page;
    unsigned char *guard=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);
    assert(!mprotect(guard+span,page,PROT_NONE));
    size_t positions[4];for(unsigned lane=0;lane<4;++lane){positions[lane]=span+page+lane*2*page;assert(!mprotect(guard+positions[lane]+page,page,PROT_NONE));}
    size_t stack=span+9*page;unsigned char *saved=malloc(size);assert(saved);
    unsigned protected_cases=0;
    for(unsigned c=0;c<sizeof cases/sizeof cases[0];++c) {
        const struct bulk_case *v=&cases[c];memset(guard,0xa6,span);memset(guard+stack,0xa6,page);
        uint32_t lanes[4];for(unsigned lane=0;lane<4;++lane){memset(guard+positions[lane],0xa6,page);lanes[lane]=0x1000+(uint32_t)(positions[lane]+page-16);memcpy(guard+positions[lane]+page-16,v->state+16*lane,16);}
        uint32_t src=0x1000+(uint32_t)span-v->processed;data_fill(guard+span-v->processed,v,v->processed);
        _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)guard-0x1000;cpu._esp=0x1000+(uint32_t)stack+0x80;memcpy(cpu._xmm[0],v->seed,16);
        uint32_t args[7]={lanes[0],lanes[1],lanes[2],lanes[3],src,v->length,v->first};memcpy(guard+stack+0x80,args,sizeof args);
        memcpy(saved,guard,span);memcpy(saved+stack,guard+stack,page);uint32_t host_return=0xfffff000;memcpy(saved+stack+0x7c,&host_return,4);
        for(unsigned lane=0;lane<4;++lane){memcpy(saved+positions[lane],guard+positions[lane],page);memcpy(saved+positions[lane]+page-16,v->result+16*lane,16);}
        _cpu wanted=cpu;wanted._eax=v->processed;memset(wanted._ymm_hi,0,sizeof wanted._ymm_hi);
        tribes_enter(&cpu,0x100c9e00);assert(!memcmp(&cpu,&wanted,sizeof cpu));assert(!memcmp(guard,saved,span));assert(!memcmp(guard+stack,saved+stack,page));
        for(unsigned lane=0;lane<4;++lane)assert(!memcmp(guard+positions[lane],saved+positions[lane],page));++protected_cases;++total;
    }
    assert(protected_cases==694);assert(!munmap(guard,size));free(saved);free(expected);free(arena);
    printf("PASS: %u native CRC bulk entry cdecl calls, full CPU and arena\n",total);return 0;
}
