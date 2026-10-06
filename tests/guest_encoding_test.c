#include "../port/core/guest_encoding.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void *worker(void *argument) {
    (void)argument;unsigned char memory[64];uint32_t pages[]={0,1,1252,437};
    for(unsigned i=0;i<1000;i++){
        memset(memory,0xa5,sizeof memory);tribes_set_last_error(123);
        assert(tribes_get_cp_info((uintptr_t)memory,pages[i%4],17)&&tribes_get_last_error()==123);
        uint32_t maximum;memcpy(&maximum,memory+17,4);assert(maximum==1&&memory[21]=='?');
        for(unsigned n=22;n<37;n++)assert(!memory[n]);
        for(unsigned n=0;n<17;n++)assert(memory[n]==0xa5);for(unsigned n=37;n<64;n++)assert(memory[n]==0xa5);
    }
    unsigned char buffer[1100];for(unsigned i=0;i<256;i++)buffer[1+i]=(unsigned char)i;
    for(unsigned iteration=0;iteration<1000;iteration++) {
        memset(buffer+512,0x79,515);tribes_set_last_error(123);
        assert(tribes_multibyte_to_wide((uintptr_t)buffer,0,iteration%2?1:9,1,256,513,256)==256&&tribes_get_last_error()==123);
        assert(buffer[512]==0x79&&buffer[1025]==0x79&&buffer[1026]==0x79);
        assert(buffer[513]==0&&buffer[514]==0&&buffer[513+128*2]==0xac&&buffer[514+128*2]==0x20);
        assert(buffer[513+129*2]==0x81&&buffer[514+129*2]==0&&buffer[513+255*2]==255&&buffer[514+255*2]==0);
        for(unsigned i=0;i<256;i++)assert(buffer[i+1]==i);
    }
    unsigned char types[128];const uint16_t chars[]={0x41,0x61,0x30,0x20,0x09,0x00aa,0x00b2,0x0081,0x20ac};
    const uint16_t expected[]={0x381,0x382,0x284,0x248,0x268,0x312,0x214,0x220,0x200};
    memcpy(types+1,chars,sizeof chars);
    for(unsigned i=0;i<1000;i++) {
        memset(types+63,0x79,22);tribes_set_last_error(123);
        assert(tribes_get_string_type_w((uintptr_t)types,1,1,9,65)&&tribes_get_last_error()==123);
        assert(!memcmp(types+65,expected,sizeof expected)&&types[64]==0x79&&types[83]==0x79);
        assert(!memcmp(types+1,chars,sizeof chars));
    }
    unsigned char casing[128];const uint16_t mixed[]={0x41,0x61,0x00e9,0x00c9,0x00df,0x00b5,0x0178,0};
    const uint16_t lower[]={0x61,0x61,0x00e9,0x00e9,0x00df,0x00b5,0x00ff,0};
    memcpy(casing+1,mixed,sizeof mixed);
    for(unsigned i=0;i<1000;i++) {
        memset(casing+63,0x79,22);tribes_set_last_error(123);
        assert(tribes_lcmap_string_ex((uintptr_t)casing,0,0x100,1,8,65,8,0,0,0)==8&&tribes_get_last_error()==123);
        assert(!memcmp(casing+65,lower,sizeof lower)&&casing[64]==0x79&&casing[81]==0x79);
        assert(!memcmp(casing+1,mixed,sizeof mixed));
    }
    unsigned char reverse[128];const uint16_t reverse_input[]={0x20ac,0x0153,0xff41,0x4e00,0x003f,0};
    const unsigned char bestfit[]={0x80,0x9c,'a','?','?',0},strict[]={0x80,0x9c,'!','!','?',0};
    memcpy(reverse+1,reverse_input,sizeof reverse_input);reverse[41]='!';
    for(unsigned i=0;i<1000;i++) {
        memset(reverse+63,0x79,16);tribes_set_last_error(123);
        assert(tribes_wide_to_multibyte((uintptr_t)reverse,0,i%2?0x400:0,1,6,65,6,i%2?41:0,73)==6&&tribes_get_last_error()==123);
        assert(!memcmp(reverse+65,i%2?strict:bestfit,6)&&reverse[64]==0x79&&reverse[71]==0x79);
        uint32_t used;memcpy(&used,reverse+73,4);assert(used==1&&reverse[72]==0x79&&reverse[77]==0x79);
        assert(!memcmp(reverse+1,reverse_input,sizeof reverse_input));
    }
    return NULL;
}
int main(void) {
    tribes_set_last_error(321);assert(tribes_get_acp()==1252&&tribes_get_oemcp()==437&&tribes_is_valid_code_page(1252)&&tribes_is_valid_code_page(437)&&tribes_get_last_error()==321);
    assert(!tribes_get_cp_info(0,1252,0)&&tribes_get_last_error()==87);
    assert(!tribes_get_cp_info(0,1252,UINT32_MAX-18)&&tribes_get_last_error()==87);
    assert(!tribes_get_cp_info(UINT64_MAX-10,437,17)&&tribes_get_last_error()==87);
    unsigned char buffer[128];memset(buffer,0x79,sizeof buffer);buffer[1]=0xe9;buffer[2]=0;buffer[3]=0x80;
    assert(tribes_multibyte_to_wide((uintptr_t)buffer,1252,0,1,UINT32_MAX,UINT32_MAX,0)==2);
    assert(tribes_multibyte_to_wide((uintptr_t)buffer,1252,1,1,UINT32_MAX,17,2)==2);
    const unsigned char expected[]={0xe9,0,0,0};assert(!memcmp(buffer+17,expected,4)&&buffer[21]==0x79);
    assert(tribes_multibyte_to_wide((uintptr_t)buffer,1252,0,1,3,17,3)==3&&buffer[21]==0xac&&buffer[22]==0x20);
    memset(buffer+17,0x79,16);assert(!tribes_multibyte_to_wide((uintptr_t)buffer,1252,0,1,3,17,2)&&tribes_get_last_error()==122);
    for(unsigned i=17;i<33;i++)assert(buffer[i]==0x79);
    assert(!tribes_multibyte_to_wide((uintptr_t)buffer,1252,3,1,3,17,3)&&tribes_get_last_error()==1004);
    assert(!tribes_multibyte_to_wide((uintptr_t)buffer,1252,0,1,0,17,3)&&tribes_get_last_error()==87);
    assert(!tribes_multibyte_to_wide((uintptr_t)buffer,1252,0,1,3,1,3)&&tribes_get_last_error()==87);
    assert(!tribes_multibyte_to_wide((uintptr_t)buffer,1252,0,UINT32_MAX,2,17,3)&&tribes_get_last_error()==87);
    assert(!tribes_multibyte_to_wide((uintptr_t)buffer,1252,0,1,3,UINT32_MAX-1,3)&&tribes_get_last_error()==87);
    assert(!tribes_multibyte_to_wide((uintptr_t)buffer,1252,0,1,UINT32_MAX-1,17,3)&&tribes_get_last_error()==87);
    unsigned char classification[64];const uint16_t terminated[]={0x41,0,0x61};memcpy(classification+1,terminated,sizeof terminated);memset(classification+17,0x79,20);
    assert(tribes_get_string_type_w((uintptr_t)classification,1,1,UINT32_MAX-1,17));
    const uint16_t terminated_types[]={0x381,0x220};assert(!memcmp(classification+17,terminated_types,4)&&classification[21]==0x79);
    assert(!tribes_get_string_type_w((uintptr_t)classification,3,1,1,17)&&tribes_get_last_error()==1004);
    assert(!tribes_get_string_type_w((uintptr_t)classification,1,1,1,1)&&tribes_get_last_error()==87);
    assert(!tribes_get_string_type_w((uintptr_t)classification,1,UINT32_MAX,1,17)&&tribes_get_last_error()==87);
    const uint16_t map_source[]={0x41,0x00c9,0};memcpy(classification+1,map_source,sizeof map_source);
    assert(tribes_lcmap_string_ex((uintptr_t)classification,0,0x100,1,UINT32_MAX-1,0,0,0,0,0)==3);
    assert(tribes_lcmap_string_ex((uintptr_t)classification,0,0x100,1,UINT32_MAX,1,3,0,0,0)==3);
    const uint16_t mapped[]={0x61,0x00e9,0};assert(!memcmp(classification+1,mapped,sizeof mapped));
    memset(classification+17,0x79,16);assert(!tribes_lcmap_string_ex((uintptr_t)classification,0,0x200,1,3,17,2,0,0,0)&&tribes_get_last_error()==122);
    for(unsigned i=17;i<33;i++)assert(classification[i]==0x79);
    const uint16_t reverse_input[]={0x20ac,0xff41,0,0x4e00};memcpy(buffer+1,reverse_input,sizeof reverse_input);
    assert(tribes_wide_to_multibyte((uintptr_t)buffer,1252,0,1,UINT32_MAX,0,0,0,0)==3);
    memset(buffer+17,0x79,12);assert(!tribes_wide_to_multibyte((uintptr_t)buffer,1252,0,1,4,17,2,0,0)&&tribes_get_last_error()==122);
    assert(buffer[17]==0x80&&buffer[18]=='a'&&buffer[19]==0x79);
    assert(!tribes_wide_to_multibyte((uintptr_t)buffer,1252,0,1,4,1,4,0,0)&&tribes_get_last_error()==87);
    assert(!tribes_wide_to_multibyte((uintptr_t)buffer,1252,0,1,4,0,4,0,0)&&tribes_get_last_error()==122);
    assert(!tribes_wide_to_multibyte((uintptr_t)buffer,1252,0x80,1,4,17,4,0,0)&&tribes_get_last_error()==1004);
    assert(!tribes_wide_to_multibyte((uintptr_t)buffer,1252,0,UINT32_MAX,4,17,4,0,0)&&tribes_get_last_error()==87);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: declared ANSI 1252/OEM 437 profile, CP_ACP/CP_OEMCP selectors, original 20-byte SBCS metadata, unaligned guards, overflow/NULL checks, LastError isolation and 8000 concurrent queries plus 8000 guarded CP1252 conversions, UTF-16 byte order, strict/default flags, NUL/count/size semantics and failure preservation, 8000 guarded table-driven classification calls, negative-count terminator and alias/flag checks, 8000 guarded case-map calls with immutable source, NUL/size/in-place semantics, 8000 guarded reverse conversions with best-fit/strict/custom replacement and BOOL output, short prefix/terminated size/error checks");return 0;
}
