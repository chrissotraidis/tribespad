#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t);
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args) {
    uint32_t top=cpu->_esp,sp=top-count*4;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;
    if(count)memcpy(memory+sp,args,count*4);memset(memory+sp-68,0xa5,64);memset(memory+top,0xa5,64);cpu->_esp=sp;_cpu before;memcpy(&before,cpu,sizeof before);
    tribes_enter(cpu,target);uint32_t result=cpu->_eax;before._eax=result;before._esp=top;assert(!memcmp(&before,cpu,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0xa5&&memory[top+i]==0xa5);
    if(count)assert(!memcmp(memory+sp,args,count*4));return result;
}
int main(int argc,char **argv) {
    (void)argc;(void)argv;
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);assert(!mprotect((char *)window+0x100000,0x10000,PROT_READ|PROT_WRITE));
#ifdef CHECKED_TEST
    assert(tribes_memory_checks_configure((uintptr_t)window,UINT64_C(0x100000000)));tribes_memory_external_configure((uintptr_t)window,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
    _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)window;cpu._esp=0x10f003;
    unsigned char expected[20]={1,0,0,0,'?'};uint32_t pages[]={0,1,1252,437};
    tribes_set_last_error(123);assert(invoke(&cpu,1,0,NULL)==1252&&invoke(&cpu,2,0,NULL)==437&&tribes_get_last_error()==123);
    for(unsigned i=0;i<1000;i++) {
        uint32_t output=0x100001+i%3;unsigned char *bytes=(unsigned char *)window+output;memset(bytes-1,0x79,22);
        uint32_t args[]={pages[i%4],output};tribes_set_last_error(123);assert(invoke(&cpu,3,2,args)==1);
        assert(!memcmp(bytes,expected,20)&&bytes[-1]==0x79&&bytes[20]==0x79&&tribes_get_last_error()==123);
        uint32_t explicit_page=i%2?437:1252;assert(invoke(&cpu,4,1,&explicit_page)==1&&tribes_get_last_error()==123);
    }
    uint32_t input=0x100801,output=0x100a01;unsigned char *source=(unsigned char *)window+input,*destination=(unsigned char *)window+output;
    for(unsigned i=0;i<256;i++)source[i]=(unsigned char)i;
    for(unsigned i=0;i<1000;i++) {
        uint32_t args[]={1252,i%2?1:9,input,256,0,0};tribes_set_last_error(123);assert(invoke(&cpu,5,6,args)==256);
        memset(destination-1,0x79,514);args[4]=output;args[5]=256;assert(invoke(&cpu,5,6,args)==256&&tribes_get_last_error()==123);
        assert(destination[-1]==0x79&&destination[512]==0x79&&destination[0]==0&&destination[1]==0);
        assert(destination[256]==0xac&&destination[257]==0x20&&destination[258]==0x81&&destination[259]==0);
        for(unsigned j=0;j<256;j++)assert(source[j]==j);
    }
    uint32_t too_small[]={1252,1,input,256,output,255};memset(destination,0x79,512);assert(!invoke(&cpu,5,6,too_small)&&tribes_get_last_error()==122);
    for(unsigned i=0;i<512;i++)assert(destination[i]==0x79);
    const uint16_t characters[]={0x41,0x61,0x30,0x20,0x09,0x00aa,0x00b2,0x0081,0x20ac};
    const uint16_t classes[]={0x381,0x382,0x284,0x248,0x268,0x312,0x214,0x220,0x200};
    memcpy(source,characters,sizeof characters);
    for(unsigned i=0;i<1000;i++) {
        memset(destination-1,0x79,22);tribes_set_last_error(123);uint32_t args[]={1,input,9,output};assert(invoke(&cpu,6,4,args)==1);
        assert(!memcmp(destination,classes,sizeof classes)&&destination[-1]==0x79&&destination[18]==0x79&&tribes_get_last_error()==123);
        assert(!memcmp(source,characters,sizeof characters));
    }
    const uint16_t case_input[]={0x41,0x61,0x00e9,0x00c9,0x00df,0x00b5,0x0178,0};
    const uint16_t case_output[]={0x61,0x61,0x00e9,0x00e9,0x00df,0x00b5,0x00ff,0};memcpy(source,case_input,sizeof case_input);
    for(unsigned i=0;i<1000;i++) {
        uint32_t args[]={0,0x100,input,8,0,0,0,0,0};tribes_set_last_error(123);assert(invoke(&cpu,7,9,args)==8);
        memset(destination-1,0x79,20);args[4]=output;args[5]=8;assert(invoke(&cpu,7,9,args)==8&&tribes_get_last_error()==123);
        assert(!memcmp(destination,case_output,sizeof case_output)&&destination[-1]==0x79&&destination[16]==0x79);
        assert(!memcmp(source,case_input,sizeof case_input));
    }
    const uint16_t reverse_input[]={0x20ac,0x0153,0xff41,0x4e00,0x003f,0};
    const unsigned char bestfit[]={0x80,0x9c,'a','?','?',0};
    const unsigned char strict[]={0x80,0x9c,'!','!','?',0};memcpy(source,reverse_input,sizeof reverse_input);
    uint32_t replacement=0x100c01,used=0x100c11;*((unsigned char *)window+replacement)='!';
    for(unsigned i=0;i<1000;i++) {
        uint32_t args[]={1252,i%2?0x400:0,input,6,0,0,i%2?replacement:0,used};
        tribes_set_last_error(123);assert(invoke(&cpu,8,8,args)==6&&tribes_get_last_error()==123);
        memset(destination-1,0x79,10);memset((char *)window+used-1,0x79,6);args[4]=output;args[5]=6;
        assert(invoke(&cpu,8,8,args)==6&&tribes_get_last_error()==123);
        assert(!memcmp(destination,i%2?strict:bestfit,6)&&destination[-1]==0x79&&destination[6]==0x79);
        uint32_t flag;memcpy(&flag,(char *)window+used,4);assert(flag==1&&*((unsigned char *)window+used-1)==0x79&&*((unsigned char *)window+used+4)==0x79);
        assert(!memcmp(source,reverse_input,sizeof reverse_input));
    }
    uint32_t short_reverse[]={1252,0,input,6,output,3,0,0};memset(destination-1,0x79,10);
    assert(!invoke(&cpu,8,8,short_reverse)&&tribes_get_last_error()==122&&!memcmp(destination,bestfit,3)&&destination[-1]==0x79&&destination[3]==0x79);
    uint32_t invalid[]={1252,0};assert(!invoke(&cpu,3,2,invalid)&&tribes_get_last_error()==87);
    /* Composition changes the required byte count; SEPCHARS retains failed
       compositions and NO_BEST_FIT applies to the composed code point. */
    const uint16_t composite[]={0x65,0x301,0x41,0x304,0x71,0x301,0};
    const unsigned char composed_best[]={0xe9,'A','q',0xb4,0},composed_strict[]={0xe9,'A','!','q','!',0};
    for(unsigned strict_mode=0;strict_mode<2;strict_mode++){
        memcpy(source,composite,sizeof composite);uint32_t args[]={1252,strict_mode?0x620u:0x220u,input,7,0,0,replacement,used};uint32_t required=strict_mode?6:5;
        tribes_set_last_error(123);assert(invoke(&cpu,8,8,args)==required&&tribes_get_last_error()==123);
        args[4]=output;args[5]=required;memset(destination-1,0x79,10);assert(invoke(&cpu,8,8,args)==required);
        assert(!memcmp(destination,strict_mode?composed_strict:composed_best,required)&&destination[-1]==0x79&&destination[required]==0x79);uint32_t flag;memcpy(&flag,(char *)window+used,4);assert(flag==strict_mode);
        args[5]=1;memset(destination,0x79,10);assert(!invoke(&cpu,8,8,args)&&tribes_get_last_error()==122&&destination[0]==0xe9&&destination[1]==0x79);memcpy(&flag,(char *)window+used,4);assert(flag==0);
        args[1]&=~0x20u;args[4]=0;args[5]=0;assert(invoke(&cpu,8,8,args)==required);
    }
    const uint16_t accent[]={0x41,0x30a,0};memcpy(source,accent,sizeof accent);uint32_t accent_args[]={0,0x220,input,UINT32_MAX,output,2,0,used};assert(invoke(&cpu,8,8,accent_args)==2&&destination[0]==0xc5&&destination[1]==0);uint32_t no_default;memcpy(&no_default,(char *)window+used,4);assert(no_default==0);
    /* Legacy W: size queries, LCID aliases, negative/explicit lengths and in-place. */
    uint32_t locales[]={0,9,0x409,0x400,0x800,0x7f,0xc00,0x1000,0x1400};
    for(unsigned i=0;i<sizeof locales/sizeof *locales;i++){
        memcpy(source,case_input,sizeof case_input);tribes_set_last_error(123);
        uint32_t args[]={locales[i],0x100,input,UINT32_MAX-1,0,0};assert(invoke(&cpu,9,6,args)==8);
        args[4]=output;args[5]=7;memset(destination,0x79,20);assert(!invoke(&cpu,9,6,args)&&tribes_get_last_error()==122);for(unsigned j=0;j<20;j++)assert(destination[j]==0x79);
        args[5]=8;tribes_set_last_error(123);assert(invoke(&cpu,9,6,args)==8&&tribes_get_last_error()==123&&!memcmp(destination,case_output,16));
        args[4]=input;assert(invoke(&cpu,9,6,args)==8&&!memcmp(source,case_output,16));
    }
    memset(source,0,2);uint32_t probe[]={0,0x100,input,1,0,0};assert(invoke(&cpu,9,6,probe)==1);probe[3]=0;assert(!invoke(&cpu,9,6,probe)&&tribes_get_last_error()==87);
    /* Every CP1252 byte: ANSI mapping must agree with wide decode/map/encode. */
    for(unsigned i=0;i<256;i++)source[i]=(unsigned char)i;
    const uint32_t wide=0x102001,mapped=0x103001,expected_bytes=0x104001,expected_types=0x105001;
    uint32_t decode[]={1252,0,input,256,wide,256};assert(invoke(&cpu,5,6,decode)==256);
    for(unsigned upper=0;upper<2;upper++){
        uint32_t map[]={0,upper?0x200u:0x100u,wide,256,mapped,256};assert(invoke(&cpu,9,6,map)==256);
        uint32_t encode[]={1252,0,mapped,256,expected_bytes,256,0,0};assert(invoke(&cpu,8,8,encode)==256);
        map[2]=input;map[4]=0;map[5]=0;assert(invoke(&cpu,10,6,map)==256);
        map[4]=output;map[5]=255;memset(destination-1,0x79,258);assert(!invoke(&cpu,10,6,map)&&tribes_get_last_error()==122);for(unsigned j=0;j<256;j++)assert(destination[j]==0x79);
        map[1]|=0x40000000;map[5]=256;tribes_set_last_error(123);assert(invoke(&cpu,10,6,map)==256&&tribes_get_last_error()==123);
        assert(!memcmp(destination,(char *)window+expected_bytes,256)&&destination[-1]==0x79&&destination[256]==0x79);
    }
    for(unsigned kind=1;kind<=4;kind*=2){
        uint32_t wargs[]={kind,wide,256,expected_types};assert(invoke(&cpu,6,4,wargs));
        uint32_t args[]={0x409,kind,input,256,output};memset(destination-1,0x79,514);tribes_set_last_error(123);assert(invoke(&cpu,11,5,args)&&tribes_get_last_error()==123);
        assert(!memcmp(destination,(char *)window+expected_types,512)&&destination[-1]==0x79&&destination[512]==0x79);
    }
    const unsigned char ansi_in[]={0xc9,'A',0,'Z'},ansi_out[]={0xe9,'a',0};memcpy(source,ansi_in,4);
    uint32_t ansi[]={0,0x100,input,UINT32_MAX,output,3};assert(invoke(&cpu,10,6,ansi)==3&&!memcmp(destination,ansi_out,3));
    ansi[4]=input;assert(invoke(&cpu,10,6,ansi)==3&&!memcmp(source,ansi_out,3)&&source[3]=='Z');
    uint32_t cls[]={0,1,input,UINT32_MAX,output};assert(invoke(&cpu,11,5,cls));uint16_t term;memcpy(&term,destination+4,2);assert(term==0x220);
    cls[3]=0;assert(!invoke(&cpu,11,5,cls)&&tribes_get_last_error()==87);
#ifdef CHECKED_TEST
    if(argc>1){assert(tribes_memory_permissions_set(0x100000,4096,argv[1][0]=='r'?0:1));uint32_t args[]={0,0x100,input,1,output,1};invoke(&cpu,9,6,args);abort();}
#endif
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: legacy LCMapString A/W and GetStringTypeA, all CP1252 bytes, LCID aliases, size/terminator/in-place/failure checks; emitted ARM64 encoding stdcall wrappers, ANSI/OEM and validity queries, 1000 unaligned guarded CPINFO outputs, preserved CPU/LastError/stack arguments NULL-output failure and 2000 six-argument CP1252 size/conversion calls with embedded NUL, C1/euro mappings and insufficient-buffer preservation plus 1000 guarded four-argument UTF-16 classification calls and 2000 nine-argument LCMapStringEx size/map calls and 2000 eight-argument reverse conversion calls with strict/best-fit/custom replacement/BOOL outputs and short-buffer prefix");return 0;
}
