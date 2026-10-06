#include "guest_encoding.h"
#include "guest_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The implemented process profile never switches file APIs to OEM. ASCII
   filenames resolve identically under its CP1252 ANSI code page. Reporting
   the default mode does not claim support for extended filename characters.
   https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-arefileapisansi */
uint32_t tribes_are_file_apis_ansi(void){return 1;}
uint32_t tribes_get_acp(void){return 1252;}
uint32_t tribes_get_oemcp(void){return 437;}
uint32_t tribes_is_valid_code_page(uint32_t page) {
    if(page==1252||page==437||page==65001)return 1;
    fprintf(stderr,"TribesPad: unsupported code-page validity query %u\n",page);abort();
}
/* Original x86 CPINFO: UINT at 0, two default bytes at 4, twelve lead bytes
   at 6, two padding bytes at 18. SBCS has no lead-byte ranges.
   https://learn.microsoft.com/en-us/windows/win32/api/winnls/ns-winnls-cpinfo */
uint32_t tribes_get_cp_info(uint64_t base,uint32_t page,uint32_t output) {
    if(page==0)page=tribes_get_acp();else if(page==1)page=tribes_get_oemcp();
    if(page!=1252&&page!=437&&page!=65001){fprintf(stderr,"TribesPad: unsupported code-page metadata %u\n",page);abort();}
    if(!output||output>UINT32_MAX-19||base>UINTPTR_MAX-output-19){tribes_set_last_error(87);return 0;}
    unsigned char info[20]={0};uint32_t maximum=page==65001?4:1;memcpy(info,&maximum,4);info[4]='?';
    memcpy((void *)(uintptr_t)(base+output),info,sizeof info);return 1;
}

#include "cp1252_table.inc"
static int range(uint64_t base,uint32_t address,uint64_t bytes) {
    uint64_t end=(uint64_t)address+bytes;
    return address&&bytes&&end<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-end;
}
/* UTF-8 follows Unicode scalar/range and maximal-subpart replacement rules.
   Strict decoding (MB_ERR_INVALID_CHARS) and encoding (WC_ERR_INVALID_CHARS)
   fail with ERROR_NO_UNICODE_TRANSLATION. Two passes validate/count before
   writing: failed conversions leave output untouched in this declared profile.
   Source/destination must be caller-owned mapped storage. */
static _Noreturn void utf8_unsupported(const char *why){fprintf(stderr,"TribesPad: UTF-8 conversion unsupported: %s\n",why);abort();}
static uint32_t utf8_length(uint64_t base,uint32_t input,uint32_t count,unsigned width){
    if(count!=UINT32_MAX){if(count>1048576)utf8_unsupported("input exceeds one-million-unit profile");return count;}
    for(uint32_t n=0;n<1048576;n++){
        if(!range(base,input,(uint64_t)(n+1)*width))return 0;
        uint16_t ch=0;memcpy(&ch,(void *)(uintptr_t)(base+input+(uint64_t)n*width),width);if(!ch)return n+1;
    }
    utf8_unsupported("unterminated input exceeds one-million-unit profile");
}
static uint32_t utf8_decode_scalar(const unsigned char *s,uint32_t length,uint32_t *at,int *valid){
    unsigned c=s[(*at)++],need,min=0x80,max=0xbf;uint32_t value;
    *valid=1;if(c<0x80)return c;
    if(c>=0xc2&&c<=0xdf){need=1;value=c&31;}
    else if(c>=0xe0&&c<=0xef){need=2;value=c&15;if(c==0xe0)min=0xa0;if(c==0xed)max=0x9f;}
    else if(c>=0xf0&&c<=0xf4){need=3;value=c&7;if(c==0xf0)min=0x90;if(c==0xf4)max=0x8f;}
    else{*valid=0;return 0xfffd;}
    for(unsigned i=0;i<need;i++){
        if(*at==length||s[*at]<min||s[*at]>max){*valid=0;return 0xfffd;}
        value=(value<<6)|(s[(*at)++]&63);min=0x80;max=0xbf;
    }
    return value;
}
static uint32_t utf16_decode_scalar(uint64_t base,uint32_t input,uint32_t length,uint32_t *at,int *valid){
    uint16_t first;memcpy(&first,(void *)(uintptr_t)(base+input+2ull*(*at)++),2);*valid=1;
    if(first<0xd800||first>0xdfff)return first;
    if(first<=0xdbff&&*at<length){uint16_t second;memcpy(&second,(void *)(uintptr_t)(base+input+2ull*(*at)),2);if(second>=0xdc00&&second<=0xdfff){(*at)++;return 0x10000u+((first-0xd800u)<<10)+(second-0xdc00u);}}
    *valid=0;return 0xfffd;
}
static int utf8_output(uint64_t base,uint32_t input,uint64_t input_bytes,uint32_t output,uint64_t bytes,uint32_t capacity,uint32_t required){
    if(!output||capacity<required){tribes_set_last_error(122);return 0;}
    if(output<0x10000||!range(base,output,bytes)){tribes_set_last_error(87);return 0;}
    if((uint64_t)input<(uint64_t)output+bytes&&(uint64_t)output<(uint64_t)input+input_bytes)utf8_unsupported("overlapping input/output buffers");
    return 1;
}
static uint32_t utf8_to_wide(uint64_t base,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity){
    if(flags!=0&&flags!=8){tribes_set_last_error(1004);return 0;}
    if(input<0x10000||!count||((int32_t)count<0&&count!=UINT32_MAX)||(int32_t)capacity<0||input==output){tribes_set_last_error(87);return 0;}
    uint32_t length=utf8_length(base,input,count,1);if(!length||!range(base,input,length)){tribes_set_last_error(87);return 0;}
    const unsigned char *s=(void *)(uintptr_t)(base+input);uint32_t at=0,required=0;int valid;
    while(at<length){uint32_t scalar=utf8_decode_scalar(s,length,&at,&valid);if(!valid&&flags){tribes_set_last_error(1113);return 0;}required+=scalar>0xffff?2:1;}
    if(!capacity)return required;if(!utf8_output(base,input,length,output,2ull*required,capacity,required))return 0;
    at=0;uint32_t written=0;while(at<length){uint32_t scalar=utf8_decode_scalar(s,length,&at,&valid);if(scalar>0xffff){uint16_t pair[2]={(uint16_t)(0xd800+((scalar-0x10000)>>10)),(uint16_t)(0xdc00+((scalar-0x10000)&1023))};memcpy((void *)(uintptr_t)(base+output+2ull*written),pair,4);written+=2;}else{uint16_t ch=(uint16_t)scalar;memcpy((void *)(uintptr_t)(base+output+2ull*written++),&ch,2);}}
    return required;
}
static uint32_t wide_to_utf8(uint64_t base,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity,uint32_t default_char,uint32_t used_default){
    if(flags!=0&&flags!=0x80){tribes_set_last_error(1004);return 0;}
    if(default_char||used_default||input<0x10000||!count||((int32_t)count<0&&count!=UINT32_MAX)||(int32_t)capacity<0||input==output){tribes_set_last_error(87);return 0;}
    uint32_t length=utf8_length(base,input,count,2);if(!length||!range(base,input,2ull*length)){tribes_set_last_error(87);return 0;}
    uint32_t at=0,required=0;int valid;while(at<length){uint32_t scalar=utf16_decode_scalar(base,input,length,&at,&valid);if(!valid&&flags){tribes_set_last_error(1113);return 0;}required+=scalar<0x80?1:scalar<0x800?2:scalar<0x10000?3:4;}
    if(!capacity)return required;if(!utf8_output(base,input,2ull*length,output,required,capacity,required))return 0;
    at=0;uint32_t written=0;unsigned char *destination=(void *)(uintptr_t)(base+output);
    while(at<length){uint32_t scalar=utf16_decode_scalar(base,input,length,&at,&valid);if(scalar<0x80)destination[written++]=(unsigned char)scalar;else if(scalar<0x800){destination[written++]=(unsigned char)(0xc0|(scalar>>6));destination[written++]=(unsigned char)(0x80|(scalar&63));}else if(scalar<0x10000){destination[written++]=(unsigned char)(0xe0|(scalar>>12));destination[written++]=(unsigned char)(0x80|((scalar>>6)&63));destination[written++]=(unsigned char)(0x80|(scalar&63));}else{destination[written++]=(unsigned char)(0xf0|(scalar>>18));destination[written++]=(unsigned char)(0x80|((scalar>>12)&63));destination[written++]=(unsigned char)(0x80|((scalar>>6)&63));destination[written++]=(unsigned char)(0x80|(scalar&63));}}
    return required;
}

/* CP1252 single-byte decoding, default/precomposed and strict-valid flags.
   Size queries count UTF-16 code units. Explicit positive lengths retain NULs;
   -1 includes the first terminator. Unsupported encodings/valid flag modes stop.
   https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar */
uint32_t tribes_multibyte_to_wide(uint64_t base,uint32_t page,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity) {
    if(page==0)page=tribes_get_acp();
    if(page==65001)return utf8_to_wide(base,flags,input,count,output,capacity);
    if(page!=1252){fprintf(stderr,"TribesPad: unsupported multibyte conversion page %u\n",page);abort();}
    if((flags&~15u)||((flags&3u)==3u)){tribes_set_last_error(1004);return 0;}
    if(flags&6u){fprintf(stderr,"TribesPad: unsupported multibyte conversion flags %u\n",flags);abort();}
    if(!input||!count||((int32_t)count<0&&count!=UINT32_MAX)||(int32_t)capacity<0||input==output){tribes_set_last_error(87);return 0;}
    uint32_t length=count;
    if(count==UINT32_MAX) {
        length=0;
        do {
            if(length==INT32_MAX||!range(base,input,(uint64_t)length+1)){tribes_set_last_error(87);return 0;}
            length++;
        } while(*(const unsigned char *)(uintptr_t)(base+input+length-1));
    } else if(!range(base,input,length)){tribes_set_last_error(87);return 0;}
    if(!capacity)return length;
    if(!output||capacity<length){tribes_set_last_error(122);return 0;}
    if(!range(base,output,(uint64_t)length*2)){tribes_set_last_error(87);return 0;}
    if((uint64_t)input<(uint64_t)output+length*2ull&&(uint64_t)output<(uint64_t)input+length){fprintf(stderr,"TribesPad: unsupported overlapping conversion buffers\n");abort();}
    const unsigned char *source=(void *)(uintptr_t)(base+input);unsigned char *destination=(void *)(uintptr_t)(base+output);
    for(uint32_t i=0;i<length;i++) {
        uint16_t value=cp1252_to_unicode[source[i]];memcpy(destination+2ull*i,&value,2);
    }
    return length;
}

#include "nls_classification.inc"
static uint16_t index_word(uint32_t offset) {
    return (uint16_t)(nls_class_index[offset]|((uint16_t)nls_class_index[offset+1]<<8));
}
static uint16_t character_type(uint16_t ch,uint32_t kind) {
    uint32_t level=index_word(2u*(ch>>8));
    uint32_t slot=index_word(level+2u*((ch>>4)&15u))+(ch&15u);
    return nls_class_values[3u*nls_class_index[slot]+kind/2u];
}
/* Data-driven CTYPE1/2/3 for UTF-16 code units using the declared Wine 10.0
   reference. Negative counts include the first terminating NUL, as documented.
   Supplementary code points are not combined into scalar-value properties.
   https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-getstringtypew */
uint32_t tribes_get_string_type_w(uint64_t base,uint32_t kind,uint32_t input,uint32_t count,uint32_t output) {
    if(kind!=1&&kind!=2&&kind!=4){tribes_set_last_error(1004);return 0;}
    if(!input||!output||input==output){tribes_set_last_error(87);return 0;}
    uint32_t length=count;
    if((int32_t)count<0) {
        length=0;uint16_t ch;
        do {
            if(length==INT32_MAX||!range(base,input,2ull*(length+1))){tribes_set_last_error(87);return 0;}
            memcpy(&ch,(void *)(uintptr_t)(base+input+2ull*length),2);length++;
        } while(ch);
    }
    if(!length)return 1;
    uint64_t bytes=2ull*length;
    if(!range(base,input,bytes)||!range(base,output,bytes)){tribes_set_last_error(87);return 0;}
    if((uint64_t)input<(uint64_t)output+bytes&&(uint64_t)output<(uint64_t)input+bytes){fprintf(stderr,"TribesPad: unsupported overlapping classification buffers\n");abort();}
    for(uint32_t i=0;i<length;i++) {
        uint16_t ch,value;memcpy(&ch,(void *)(uintptr_t)(base+input+2ull*i),2);value=character_type(ch,kind);
        memcpy((void *)(uintptr_t)(base+output+2ull*i),&value,2);
    }
    return 1;
}

#include "nls_casing.inc"
static int supported_locale(uint64_t base,uint32_t locale) {
    if(!locale)return 1; /* configured user-default profile */
    char name[86];unsigned i;
    for(i=0;i<85;i++) {
        uint16_t ch;
        if(!range(base,locale,2ull*(i+1)))return 0;
        memcpy(&ch,(void *)(uintptr_t)(base+locale+2ull*i),2);
        if(ch>127)return 0;
        name[i]=(char)(ch>='A'&&ch<='Z'?ch+32:ch);
        if(!ch)break;
    }
    if(i==85)return 0;
    return !name[0]||!strcmp(name,"en-us")||!strcmp(name,"en");
}
static uint16_t case_character(const uint16_t *table,uint16_t ch) {
    uint32_t block=table[ch>>8]+((ch>>4)&15u);
    uint32_t delta=table[block]+(ch&15u);
    return (uint16_t)(ch+table[delta]);
}
/* Default (non-linguistic) upper/lower mapping for the English/invariant
   profile. Version/sort handles, other transformations/locales and supplementary
   pairs stop before mutation. Exact in-place case conversion is supported. */
uint32_t tribes_lcmap_string_ex(uint64_t base,uint32_t locale,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity,uint32_t version,uint32_t reserved,uint32_t sort_handle) {
    if(version||reserved||sort_handle){fprintf(stderr,"TribesPad: unsupported LCMapStringEx optional context\n");abort();}
    if(!supported_locale(base,locale)){fprintf(stderr,"TribesPad: unsupported LCMapStringEx locale\n");abort();}
    if(flags!=0x100&&flags!=0x200){fprintf(stderr,"TribesPad: unsupported LCMapStringEx flags %08x\n",flags);abort();}
    if(!input||!count||(int32_t)capacity<0){tribes_set_last_error(87);return 0;}
    uint32_t length=count;
    if((int32_t)count<0) {
        length=0;uint16_t ch;
        do {
            if(length==INT32_MAX||!range(base,input,2ull*(length+1))){tribes_set_last_error(87);return 0;}
            memcpy(&ch,(void *)(uintptr_t)(base+input+2ull*length),2);length++;
        } while(ch);
    }
    uint64_t bytes=2ull*length;
    if(!range(base,input,bytes)){tribes_set_last_error(87);return 0;}
    /* The default BMP table cannot stand in for supplementary scalar mapping. */
    uint16_t previous=0;
    for(uint32_t i=0;i<length;i++) {
        uint16_t ch;memcpy(&ch,(void *)(uintptr_t)(base+input+2ull*i),2);
        if(previous>=0xd800&&previous<=0xdbff&&ch>=0xdc00&&ch<=0xdfff){fprintf(stderr,"TribesPad: unsupported supplementary LCMapStringEx input\n");abort();}
        previous=ch;
    }
    if(!capacity)return length;
    if(!output||capacity<length){tribes_set_last_error(122);return 0;}
    if(!range(base,output,bytes)){tribes_set_last_error(87);return 0;}
    if(input!=output&&(uint64_t)input<(uint64_t)output+bytes&&(uint64_t)output<(uint64_t)input+bytes){fprintf(stderr,"TribesPad: unsupported overlapping case-map buffers\n");abort();}
    const uint16_t *table=flags==0x100?nls_lower_case:nls_upper_case;
    for(uint32_t i=0;i<length;i++) {
        uint16_t ch,value;memcpy(&ch,(void *)(uintptr_t)(base+input+2ull*i),2);value=case_character(table,ch);
        memcpy((void *)(uintptr_t)(base+output+2ull*i),&value,2);
    }
    return length;
}

static uint8_t reverse_character(uint16_t ch) {
    size_t left=0,right=sizeof cp1252_from_unicode/sizeof cp1252_from_unicode[0];
    while(left<right) {
        size_t middle=left+(right-left)/2;
        if(cp1252_from_unicode[middle].unicode<ch)left=middle+1;else right=middle;
    }
    return left<sizeof cp1252_from_unicode/sizeof cp1252_from_unicode[0]&&cp1252_from_unicode[left].unicode==ch?cp1252_from_unicode[left].byte:0x3f;
}
/* Legacy LCID entry points used by the bundled CRT. Match the fixed
   English/default/invariant profile already used by LCMapStringEx and the
   locale identifier service. Other locales require their actual code page and
   linguistic behavior; do not substitute the host locale.
   https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringw */
static void legacy_locale(uint32_t locale) {
    switch(locale) {
    case 0:case 0x400:case 0x800:case 0xc00:case 0x1000:case 0x1400:
    case 9:case 0x409:case 0x7f:return;
    default:fprintf(stderr,"TribesPad: unsupported legacy mapping LCID %08x\n",locale);abort();
    }
}
uint32_t tribes_lcmap_string_w(uint64_t base,uint32_t locale,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity) {
    legacy_locale(locale);
    return tribes_lcmap_string_ex(base,0,flags,input,count,output,capacity,0,0,0);
}
/* ANSI case mapping is decode -> shared Unicode case table -> the existing
   CP1252 best-fit reverse table. This profile is SBCS: every mapped character
   occupies one byte. A negative mapping count includes the terminating NUL.
   https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringa */
uint32_t tribes_lcmap_string_a(uint64_t base,uint32_t locale,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity) {
    legacy_locale(locale);flags&=~UINT32_C(0x40000000); /* LOCALE_USE_CP_ACP: same CP1252 profile */
    if(flags!=0x100&&flags!=0x200){fprintf(stderr,"TribesPad: unsupported LCMapStringA flags %08x\n",flags);abort();}
    if(!input||!count||(int32_t)capacity<0){tribes_set_last_error(87);return 0;}
    uint32_t length=count;
    if((int32_t)count<0) {
        length=0;
        do {if(length==INT32_MAX||!range(base,input,(uint64_t)length+1)){tribes_set_last_error(87);return 0;}length++;}
        while(*(const unsigned char *)(uintptr_t)(base+input+length-1));
    }
    if(!range(base,input,length)){tribes_set_last_error(87);return 0;}
    if(!capacity)return length;
    if(!output||capacity<length){tribes_set_last_error(122);return 0;}
    if(!range(base,output,length)){tribes_set_last_error(87);return 0;}
    if(input!=output&&(uint64_t)input<(uint64_t)output+length&&(uint64_t)output<(uint64_t)input+length){fprintf(stderr,"TribesPad: unsupported overlapping ANSI case-map buffers\n");abort();}
    const uint16_t *table=flags==0x100?nls_lower_case:nls_upper_case;
    for(uint32_t i=0;i<length;i++) {
        uint16_t ch=cp1252_to_unicode[*(const unsigned char *)(uintptr_t)(base+input+i)];
        *(unsigned char *)(uintptr_t)(base+output+i)=reverse_character(case_character(table,ch));
    }
    return length;
}
/* SBCS ANSI classification consumes bytes and writes one WORD per converted
   character using the same pinned CTYPE1/2/3 data as the wide API.
   https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getstringtypea */
uint32_t tribes_get_string_type_a(uint64_t base,uint32_t locale,uint32_t kind,uint32_t input,uint32_t count,uint32_t output) {
    legacy_locale(locale);
    if(kind!=1&&kind!=2&&kind!=4){tribes_set_last_error(1004);return 0;}
    if(!input||!output||!count||((int32_t)count<0&&count!=UINT32_MAX)){tribes_set_last_error(87);return 0;}
    uint32_t length=count;
    if(count==UINT32_MAX) {
        length=0;
        do {if(length==INT32_MAX||!range(base,input,(uint64_t)length+1)){tribes_set_last_error(87);return 0;}length++;}
        while(*(const unsigned char *)(uintptr_t)(base+input+length-1));
    }
    if(!range(base,input,length)||!range(base,output,2ull*length)){tribes_set_last_error(87);return 0;}
    if((uint64_t)input<(uint64_t)output+2ull*length&&(uint64_t)output<(uint64_t)input+length){fprintf(stderr,"TribesPad: unsupported overlapping ANSI classification buffers\n");abort();}
    for(uint32_t i=0;i<length;i++) {
        uint16_t ch=cp1252_to_unicode[*(const unsigned char *)(uintptr_t)(base+input+i)],value=character_type(ch,kind);
        memcpy((void *)(uintptr_t)(base+output+2ull*i),&value,2);
    }
    return 1;
}

#include "nls_composition.inc"
/* Win32 SBCS composition examines adjacent UTF-16 units. With SEPCHARS,
   a composed character without a usable code-page mapping leaves both units
   separate. This is deliberately not general NFC string normalization. */
static uint16_t reverse_next(uint64_t base,uint32_t input,uint32_t length,uint32_t *at,uint32_t flags) {
    uint16_t first;memcpy(&first,(void *)(uintptr_t)(base+input+2ull*(*at)++),2);
    if(!(flags&0x200)||*at==length)return first;
    uint16_t second;memcpy(&second,(void *)(uintptr_t)(base+input+2ull*(*at)),2);
    uint32_t key=((uint32_t)first<<16)|second;size_t lo=0,hi=sizeof nls_composition/sizeof nls_composition[0];
    while(lo<hi){size_t mid=lo+(hi-lo)/2;uint32_t k=((uint32_t)nls_composition[mid].first<<16)|nls_composition[mid].second;if(k<key)lo=mid+1;else hi=mid;}
    if(lo<sizeof nls_composition/sizeof nls_composition[0]&&nls_composition[lo].first==first&&nls_composition[lo].second==second){
        uint16_t composed=nls_composition[lo].composed;uint8_t byte=reverse_character(composed);
        if(!(((flags&0x400)||byte==0x3f)&&cp1252_to_unicode[byte]!=composed)){(*at)++;return composed;}
    }
    return first;
}

/* Pinned Microsoft CP1252 exact/best-fit reverse table. flags 0 and
   WC_NO_BEST_FIT_CHARS, optional SBCS replacement and unaligned BOOL output.
   BMP pair composition supports COMPOSITECHECK with SEPCHARS (the default).
   Surrogate input, DISCARDNS and DEFAULTCHAR composition modes remain gated. Short buffers
   receive the converted prefix before ERROR_INSUFFICIENT_BUFFER, per Win32.
   https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte */
uint32_t tribes_wide_to_multibyte(uint64_t base,uint32_t page,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity,uint32_t default_char,uint32_t used_default) {
    if(page==0)page=tribes_get_acp();
    if(page==65001)return wide_to_utf8(base,flags,input,count,output,capacity,default_char,used_default);
    if(page!=1252){fprintf(stderr,"TribesPad: unsupported reverse conversion page %u\n",page);abort();}
    if(flags&~0x670u||flags&0x80u){tribes_set_last_error(1004);return 0;}
    if(flags!=0&&flags!=0x400&&flags!=0x200&&flags!=0x220&&flags!=0x600&&flags!=0x620){fprintf(stderr,"TribesPad: unsupported reverse conversion flags %08x\n",flags);abort();}
    if(!input||!count||((int32_t)count<0&&count!=UINT32_MAX)||(int32_t)capacity<0||input==output){tribes_set_last_error(87);return 0;}
    uint32_t length=count;
    if(count==UINT32_MAX) {
        length=0;uint16_t ch;
        do {
            if(length==INT32_MAX||!range(base,input,2ull*(length+1))){tribes_set_last_error(87);return 0;}
            memcpy(&ch,(void *)(uintptr_t)(base+input+2ull*length),2);length++;
        } while(ch);
    }
    uint64_t bytes=2ull*length;
    if(!range(base,input,bytes)||(default_char&&!range(base,default_char,1))||(used_default&&!range(base,used_default,4))){tribes_set_last_error(87);return 0;}
    if(capacity&&!output){tribes_set_last_error(122);return 0;}
    uint32_t required=length;
    for(uint32_t i=0;i<length;i++) {
        uint16_t ch;memcpy(&ch,(void *)(uintptr_t)(base+input+2ull*i),2);
        if(ch>=0xd800&&ch<=0xdfff){fprintf(stderr,"TribesPad: unsupported reverse UTF-16 surrogate input\n");abort();}
    }
    if(flags&0x200){uint32_t at=0;required=0;while(at<length){reverse_next(base,input,length,&at,flags);required++;}}
    uint32_t written=capacity&&capacity<required?capacity:required;
    if(capacity&&!range(base,output,written)){tribes_set_last_error(87);return 0;}
    if(capacity&&(uint64_t)input<(uint64_t)output+written&&(uint64_t)output<(uint64_t)input+bytes){fprintf(stderr,"TribesPad: unsupported overlapping reverse buffers\n");abort();}
    if(used_default&&(((uint64_t)used_default<(uint64_t)input+bytes&&(uint64_t)input<(uint64_t)used_default+4)||(capacity&&(uint64_t)used_default<(uint64_t)output+written&&(uint64_t)output<(uint64_t)used_default+4))){fprintf(stderr,"TribesPad: unsupported overlapping reverse BOOL output\n");abort();}
    uint8_t replacement=default_char?*(const uint8_t *)(uintptr_t)(base+default_char):0x3f;
    uint32_t used=0,at=0;
    for(uint32_t i=0;i<written;i++) {
        uint16_t ch=reverse_next(base,input,length,&at,flags);
        uint8_t value=reverse_character(ch);
        if(((flags&0x400)||value==0x3f)&&cp1252_to_unicode[value]!=ch){value=replacement;used=1;}
        if(capacity)*(uint8_t *)(uintptr_t)(base+output+i)=value;
    }
    if(used_default)memcpy((void *)(uintptr_t)(base+used_default),&used,4);
    if(capacity&&capacity<required){tribes_set_last_error(122);return 0;}
    return required;
}
