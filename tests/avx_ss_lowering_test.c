#include "../port/llasm-support/llasm_cpu.h"
#include "../port/llasm-support/tribes_sse.h"
#define LITTLEENDIAN 1
#include "softfloat.h"
#include <assert.h>
#include <fcntl.h>
#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
typedef struct {
    unsigned index,flags,mxcsr,expected_mxcsr,expected_flags,regs[8],guest_base;
    unsigned char initial[256],expected[256],memory[64],after[64];
} Case;
_Static_assert(sizeof(Case)==696&&offsetof(Case,initial)==56,"binary fixture ABI mismatch");
static const Case *cases;
static size_t case_count;
#include "bodies.inc"
static void *worker(void *unused) {
    (void)unused;
    for(unsigned i=0;i<case_count;i++) {
        const Case *test=&cases[i];unsigned char backing[97];
        memset(backing,0x79,sizeof backing);unsigned char *data=backing+17;
        memcpy(data,test->memory,64);
        _cpu cpu;memset(&cpu,0xa5,sizeof cpu);memcpy(&cpu._eax,test->regs,32);
        cpu._pointer_offset=(uintptr_t)data-test->guest_base;cpu._eflags=test->flags;cpu._mxcsr=test->mxcsr;
        memcpy(cpu._xmm,test->initial,128);memcpy(cpu._ymm_hi,test->initial+128,128);
        _cpu before=cpu;_cpu expected=cpu;expected._mxcsr=test->expected_mxcsr;expected._eflags=test->expected_flags;memcpy(expected._xmm,test->expected,128);
        memcpy(expected._ymm_hi,test->expected+128,128);
        uint_fast8_t rounding=i%5,tininess=i%2,status=i%32,precision=(i%3)*16+32;
        softfloat_roundingMode=rounding;softfloat_detectTininess=tininess;
        softfloat_exceptionFlags=status;extF80_roundingPrecision=precision;
        bodies[test->index](&cpu);
        assert(softfloat_roundingMode==rounding&&softfloat_detectTininess==tininess);
        assert(softfloat_exceptionFlags==status&&extF80_roundingPrecision==precision);
        if(memcmp(&cpu,&expected,sizeof cpu)) {
            for(size_t byte=0;byte<sizeof cpu;byte++)if(((unsigned char *)&cpu)[byte]!=((unsigned char *)&expected)[byte]) {
                fprintf(stderr,"CPU mismatch case=%u body=%u byte=%zu actual=%02x expected=%02x\n",i,test->index,byte,((unsigned char *)&cpu)[byte],((unsigned char *)&expected)[byte]);break;
            }
            abort();
        }
        assert(!memcmp(data,test->after,64));
        if(unmasked_move[test->index]) {
            cpu=before;memcpy(data,test->memory,64);cpu._mxcsr&=~0x1f80u;
            expected._mxcsr=cpu._mxcsr;bodies[test->index](&cpu);
            assert(!memcmp(&cpu,&expected,sizeof cpu));assert(!memcmp(data,test->after,64));
            assert(softfloat_roundingMode==rounding&&softfloat_detectTininess==tininess);
            assert(softfloat_exceptionFlags==status&&extF80_roundingPrecision==precision);
        }
        for(unsigned n=0;n<17;n++)assert(backing[n]==0x79);
        for(unsigned n=81;n<sizeof backing;n++)assert(backing[n]==0x79);
    }
    return NULL;
}
static void unmasked_boundary(unsigned mode) {
    unsigned char *shared=mmap(NULL,sizeof(_cpu)+8,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
    assert(shared!=MAP_FAILED);_cpu *cpu=(_cpu *)shared;memset(cpu,0xa5,sizeof *cpu);
    cpu->_mxcsr=mode>=10?0x0f80:mode>=8?0x1e80:0x1f00;memset(cpu->_xmm[1],0,8);
    uint64_t input=mode>=10?UINT64_C(0x4000000000000000):mode>=8?1:
        mode>=6?UINT64_C(0xbff0000000000000):mode>=4?UINT64_C(0x7ff0000000800000):mode>=2?0x7fa00000:0x7f800000;
    if(mode>=12&&mode<22) {
        unsigned category=(mode-12)/2;
        cpu->_mxcsr=(unsigned[]){0x1f00,0x1e80,0x1b80,0x1780,0x0f80,0x1d80}[category];
        input=(uint64_t[]){UINT64_C(0x7ff0000000012345),1,UINT64_C(0x7fefffffffffffff),UINT64_C(0x3690000000000000),UINT64_C(0x3ff0000000000001)}[category];
    }
    if(mode>=22&&mode<28) {
        unsigned category=(mode-22)/2;
        cpu->_mxcsr=category==2?0x1e80:0x1f00;
        input=(uint32_t[]){0x7fc12345,0x7fa12345,1}[category];
    }
    if(mode>=28&&mode<40) {
        unsigned category=(mode-28)/2;
        cpu->_mxcsr=(unsigned[]){0x1f00,0x1e80,0x1d80,0x1b80,0x1780,0x0f80}[category];
        uint32_t left=(uint32_t[]){0,1,0x3f800000,0x7f7fffff,1,0x3f800000}[category];
        memcpy(cpu->_xmm[1],&left,4);
        input=(uint32_t[]){0,1,0,0x3f000000,0x40000000,0x40400000}[category];
    }
    if(mode>=40) {
        unsigned category=(mode-40)/2;
        cpu->_mxcsr=(unsigned[]){0x1f00,0x1e80,0x1b80,0x9780,0x0f80}[category];
        uint32_t left=(uint32_t[]){0x7f800000,1,0x7f7fffff,0x00800000,0x3f800000}[category];
        memcpy(cpu->_xmm[1],&left,4);
        input=(uint32_t[]){0x7f800000,1,0xff7fffff,0x007fffff,0x33000000}[category];
    }
    unsigned char source[8];for(unsigned i=0;i<8;i++)source[i]=(unsigned char)(input>>(8*i));
    memcpy(cpu->_xmm[2],source,8);memcpy(shared+sizeof *cpu,source,8);
    cpu->_pointer_offset=(uintptr_t)(shared+sizeof *cpu)-0x2000;
    _cpu expected=*cpu;expected._mxcsr|=mode>=10?32:mode>=8?2:1;
    if(mode>=12&&mode<22)expected._mxcsr=cpu->_mxcsr|(unsigned[]){1,50,40,48,32}[(mode-12)/2];
    if(mode>=22&&mode<28)expected._mxcsr=cpu->_mxcsr|((mode-22)/2==2?2:1);
    if(mode>=28&&mode<40)expected._mxcsr=cpu->_mxcsr|(unsigned[]){1,2,4,40,50,32}[(mode-28)/2];
    if(mode>=40)expected._mxcsr=cpu->_mxcsr|(unsigned[]){1,2,40,50,32}[(mode-40)/2];
    pid_t child=fork();assert(child>=0);
    if(!child) {
        if(mode>=40) {
            if(mode&1)tb_avx_scalarmem(cpu,5,0,1,0x2000);
            else tb_avx_scalar(cpu,5,0,1,2);
        } else if(mode>=28) {
            if(mode&1)tb_avx_scalarmem(cpu,4,0,1,0x2000);
            else tb_avx_scalar(cpu,4,0,1,2);
        } else if(mode>=22) {
            if(mode&1)tb_avx_comissmem(cpu,1,0x2000);
            else tb_avx_comiss(cpu,1,2);
        } else if(mode>=12) {
            if(mode&1)tb_avx_sd2ssmem(cpu,0,1,0x2000);
            else tb_avx_sd2ss(cpu,0,1,2);
        } else if(mode>=6) {
            if(mode&1)tb_avx_sqrtsdmem(cpu,0,1,0x2000);
            else tb_avx_sqrtsd(cpu,0,1,2);
        } else if(mode==5)tb_sse_ucomisdmem(cpu,1,0x2000);
        else if(mode==4)tb_sse_ucomisd(cpu,1,2);
        else if(mode==3)tb_avx_ss2sdmem(cpu,0,1,0x2000);
        else if(mode==2)tb_avx_ss2sd(cpu,0,1,2);
        else if(mode==1)tb_avx_scalarmem(cpu,2,0,1,0x2000);
        else tb_avx_scalar(cpu,2,0,1,2);
        _exit(0);
    }
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);
    assert(!memcmp(cpu,&expected,sizeof *cpu));assert(!memcmp(shared+sizeof *cpu,source,8));
    assert(!munmap(shared,sizeof(_cpu)+8));
}
static void packed_unmasked_boundary(unsigned sub,unsigned wide,unsigned mem,unsigned category) {
    unsigned char *shared=mmap(NULL,sizeof(_cpu)+64,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
    assert(shared!=MAP_FAILED);memset(shared,0xa5,sizeof(_cpu)+64);_cpu *cpu=(_cpu *)shared;
    cpu->_mxcsr=(unsigned[]){0x1f00,0x1e80,0x1b80,0x1780,0x0f80,0x1d80}[category];
    if(sub&&sub!=3&&category==3)cpu->_mxcsr|=0x8000;
    unsigned char a[32],b[32];uint32_t one=0x3f800000;
    for(unsigned lane=0;lane<8;lane++){memcpy(a+lane*4,&one,4);memcpy(b+lane*4,&one,4);}
    unsigned lane=wide?7:3;
    uint32_t left=sub==3?(uint32_t[]){0,1,0x7f7fffff,0x00800000,0x3f800000,0x3f800000}[category]:sub?(uint32_t[]){0x7f800000,1,0x7f7fffff,0x00800000,0x3f800000}[category]:(uint32_t[]){0,1,0x7f7fffff,0x00800000,0x3f800001}[category];
    uint32_t right=sub==3?(uint32_t[]){0,1,0x00800000,0x7f7fffff,0x40400000,0}[category]:sub?(uint32_t[]){0x7f800000,1,0xff7fffff,0x007fffff,0x33000000}[category]:(uint32_t[]){0x7f800000,1,0x40000000,0x00800000,0x3f800001}[category];
    if(sub==2){right^=0x80000000;for(unsigned j=0;j<8;j++){uint32_t negative_one=0xbf800000;memcpy(b+j*4,&negative_one,4);}}
    memcpy(a+lane*4,&left,4);memcpy(b+lane*4,&right,4);
    memcpy(cpu->_xmm[1],a,16);memcpy(cpu->_ymm_hi[1],a+16,16);memcpy(cpu->_xmm[2],b,16);memcpy(cpu->_ymm_hi[2],b+16,16);
    memcpy(shared+sizeof *cpu+1,b,32);unsigned char before_memory[64];memcpy(before_memory,shared+sizeof *cpu,64);
    cpu->_pointer_offset=(uintptr_t)(shared+sizeof *cpu)-0x2000;
    _cpu expected=*cpu;expected._mxcsr|=sub==3?(unsigned[]){1,2,40,48,32,4}[category]:sub?(unsigned[]){1,2,40,50,32}[category]:(unsigned[]){1,50,40,48,32}[category];
    pid_t child=fork();assert(child>=0);
    if(!child){if(mem)tb_avx_packedmem(cpu,wide,sub==3?4:sub==2?3:sub?5:2,0,1,0x2001);else tb_avx_packed(cpu,wide,sub==3?4:sub==2?3:sub?5:2,0,1,2);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);
    assert(!memcmp(cpu,&expected,sizeof *cpu)&&!memcmp(shared+sizeof *cpu,before_memory,64));assert(!munmap(shared,sizeof(_cpu)+64));
}
static void alignment_boundary(unsigned store,unsigned wide,unsigned offset) {
    unsigned char *shared=mmap(NULL,sizeof(_cpu)+64,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
    assert(shared!=MAP_FAILED);_cpu *cpu=(_cpu *)shared;memset(shared,0xa5,sizeof(_cpu)+64);
    cpu->_mxcsr=0x1f80;cpu->_pointer_offset=(uintptr_t)(shared+sizeof *cpu)-0x2000;
    _cpu expected=*cpu;pid_t child=fork();assert(child>=0);
    if(!child) {
        if(wide) {if(store)tb_avx_aps256store(cpu,2,0x2000+offset);else tb_avx_aps256load(cpu,0,0x2000+offset);}
        else {if(store)tb_avx_apsstore(cpu,2,0x2000+offset);else tb_avx_apsload(cpu,0,0x2000+offset);}
        _exit(0);
    }
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);
    assert(!memcmp(cpu,&expected,sizeof *cpu));
    for(unsigned i=0;i<64;i++)assert(shared[sizeof *cpu+i]==0xa5);
    assert(!munmap(shared,sizeof(_cpu)+64));
}
static void emitted_alignment_boundary(const Case *test) {
    unsigned char *shared=mmap(NULL,sizeof(_cpu)+64,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
    assert(shared!=MAP_FAILED);_cpu *cpu=(_cpu *)shared;memset(cpu,0xa5,sizeof *cpu);
    memcpy(&cpu->_eax,test->regs,32);cpu->_esi++;
    cpu->_pointer_offset=(uintptr_t)(shared+sizeof *cpu)-test->guest_base;
    cpu->_mxcsr=test->mxcsr;cpu->_eflags=test->flags;
    memcpy(cpu->_xmm,test->initial,128);memcpy(cpu->_ymm_hi,test->initial+128,128);
    memcpy(shared+sizeof *cpu,test->memory,64);_cpu expected=*cpu;
    pid_t child=fork();assert(child>=0);
    if(!child){bodies[test->index](cpu);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);
    assert(!memcmp(cpu,&expected,sizeof *cpu));assert(!memcmp(shared+sizeof *cpu,test->memory,64));
    assert(!munmap(shared,sizeof(_cpu)+64));
}
int main(int argc,char **argv) {
    for(unsigned sub=0;sub<4;sub++)for(unsigned wide=0;wide<2;wide++)for(unsigned mem=0;mem<2;mem++)for(unsigned category=0;category<(sub==3?6:5);category++)packed_unmasked_boundary(sub,wide,mem,category);
    assert(argc==2);int fd=open(argv[1],O_RDONLY);assert(fd>=0);struct stat info;
    assert(!fstat(fd,&info)&&info.st_size>0&&(uint64_t)info.st_size%sizeof(Case)==0);
    case_count=(size_t)info.st_size/sizeof(Case);
    cases=mmap(NULL,(size_t)info.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    assert(cases!=MAP_FAILED&&!close(fd));
    for(unsigned mode=0;mode<50;mode++)unmasked_boundary(mode);
    alignment_boundary(0,0,1);alignment_boundary(1,0,1);
    for(unsigned offset=1;offset<32;offset++){alignment_boundary(0,1,offset);alignment_boundary(1,1,offset);}
    unsigned last=~0u;
    for(size_t i=0;i<case_count;i++)if(alignment_probe[cases[i].index]&&cases[i].index!=last){emitted_alignment_boundary(&cases[i]);last=cases[i].index;}
    worker(NULL);pthread_t threads[8];
    for(unsigned n=0;n<8;n++)assert(!pthread_create(&threads[n],NULL,worker,NULL));
    for(unsigned n=0;n<8;n++)assert(!pthread_join(threads[n],NULL));
    printf("PASS: %zu Rosetta-reference cases execute actual AOT VEX scalar bodies, full CPU/YMM/MXCSR/flags, SoftFloat TLS restoration and guarded memory on eight native threads; unserviced exceptions preserve destination lanes; emitted unmasked moves retain MXCSR\n",case_count);assert(!munmap((void *)cases,(size_t)info.st_size));return 0;
}
