/* Wine 10.0 collation adaptation; LGPL-2.1-or-later.
   Original contributors and license in nls_collation.inc/vendor/wine-nls/LICENSE.
   Uses pinned default NLS GUID for English/invariant process profile. */
#include "guest_collation.h"
#include "guest_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>
typedef uint8_t BYTE;typedef uint16_t WCHAR;typedef uint32_t UINT,DWORD;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define HANGUL_SBASE 0xac00
#define HANGUL_VCOUNT 21
#define HANGUL_TCOUNT 28
#define LOCALE_USE_CP_ACP             0x40000000
#define NORM_IGNORECASE            0x00000001
#define NORM_IGNORENONSPACE        0x00000002
#define NORM_IGNORESYMBOLS         0x00000004
#define SORT_DIGITSASNUMBERS       0x00000008
#define LINGUISTIC_IGNORECASE      0x00000010
#define LINGUISTIC_IGNOREDIACRITIC 0x00000020
#define SORT_STRINGSORT            0x00001000 /* Take punctuation into account */
#define NORM_IGNOREKANATYPE        0x00010000
#define NORM_IGNOREWIDTH           0x00020000
#define NORM_LINGUISTIC_CASING     0x08000000
struct sortguid {UINT flags,compr,except,ling_except,casemap;};
/* flags for sortguid */
#define FLAG_HAS_3_BYTE_WEIGHTS 0x01
#define FLAG_REVERSEDIACRITICS  0x10
#define FLAG_DOUBLECOMPRESSION  0x20
#define FLAG_INVERSECASING      0x40

struct sort_expansion
{
    WCHAR exp[2];
};

struct jamo_sort
{
    BYTE is_old;
    BYTE leading;
    BYTE vowel;
    BYTE trailing;
    BYTE weight;
    BYTE pad[3];
};

union char_weights
{
    UINT val;
    struct { BYTE primary, script, diacritic, _case; };
};

/* bits for case weights */
#define CASE_FULLWIDTH   0x01  /* full width kana (vs. half width) */
#define CASE_FULLSIZE    0x02  /* full size kana (vs. small) */
#define CASE_SUBSCRIPT   0x08  /* sub/super script */
#define CASE_UPPER       0x10  /* upper case */
#define CASE_KATAKANA    0x20  /* katakana (vs. hiragana) */
#define CASE_COMPR_2     0x40  /* compression exists for >= 2 chars */
#define CASE_COMPR_4     0x80  /* compression exists for >= 4 chars */
#define CASE_COMPR_6     0xc0  /* compression exists for >= 6 chars */

enum sortkey_script
{
    SCRIPT_UNSORTABLE = 0,
    SCRIPT_NONSPACE_MARK = 1,
    SCRIPT_EXPANSION = 2,
    SCRIPT_EASTASIA_SPECIAL = 3,
    SCRIPT_JAMO_SPECIAL = 4,
    SCRIPT_EXTENSION_A = 5,
    SCRIPT_PUNCTUATION = 6,
    SCRIPT_SYMBOL_1 = 7,
    SCRIPT_SYMBOL_2 = 8,
    SCRIPT_SYMBOL_3 = 9,
    SCRIPT_SYMBOL_4 = 10,
    SCRIPT_SYMBOL_5 = 11,
    SCRIPT_SYMBOL_6 = 12,
    SCRIPT_DIGIT = 13,
    SCRIPT_LATIN = 14,
    SCRIPT_GREEK = 15,
    SCRIPT_CYRILLIC = 16,
    SCRIPT_KANA = 34,
    SCRIPT_HEBREW = 40,
    SCRIPT_ARABIC = 41,
    SCRIPT_PUA_FIRST = 169,
    SCRIPT_PUA_LAST = 175,
    SCRIPT_CJK_FIRST = 192,
    SCRIPT_CJK_LAST = 239,
};

static _Noreturn void collation_unsupported(const char *why){fprintf(stderr,"TribesPad: collation unsupported: %s\n",why);abort();}
static int default_compression_weights(UINT compression,const WCHAR *tables[8]){(void)tables;if(compression!=UINT32_MAX)collation_unsupported("unexpected default compression table");return 0;}
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
#include "nls_collation.inc"
#pragma clang diagnostic pop
static int valid_range(uint64_t base,uint32_t pointer,uint64_t size){return pointer>=0x10000&&(uint64_t)pointer+size<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-pointer-size;}
static uint16_t read_unit(uint64_t base,uint32_t pointer,uint32_t i){uint16_t v;memcpy(&v,(void *)(uintptr_t)(base+pointer+2ull*i),2);return v;}
static void locale_profile(uint64_t base,uint32_t locale){
 if(!locale)return;
 char name[86];unsigned i;
 for(i=0;i<85;i++){if(!valid_range(base,locale,2ull*(i+1)))collation_unsupported("locale range");uint16_t ch=read_unit(base,locale,i);if(ch>127)collation_unsupported("locale name");name[i]=(char)(ch>='A'&&ch<='Z'?ch+32:ch);if(!ch)break;}
 if(i==85|| (name[0]&&strcmp(name,"en-us")&&strcmp(name,"en")&&strcmp(name,"!x-sys-default-locale")))collation_unsupported("locale outside English/invariant profile");
}
static uint16_t *copy_input(uint64_t base,uint32_t pointer,uint32_t count,uint32_t *length){
 if(!pointer||!valid_range(base,pointer,0))return NULL;
 uint32_t n=count;
 if((int32_t)count<0){for(n=0;n<1048576;n++){if(!valid_range(base,pointer,2ull*(n+1)))return NULL;if(!read_unit(base,pointer,n))break;}}
 if(n>1048576||((int32_t)count<0&&n==1048576))collation_unsupported("string exceeds bounded profile");
 if(!valid_range(base,pointer,2ull*n))return NULL;
 uint16_t *copy=malloc(2ull*(n?n:1));if(!copy)collation_unsupported("native input allocation failure");if(n)memcpy(copy,(void *)(uintptr_t)(base+pointer),2ull*n);*length=n;return copy;
}
uint32_t tribes_compare_string_ex(uint64_t base,uint32_t locale,uint32_t flags,uint32_t a,uint32_t na,uint32_t b,uint32_t nb,uint32_t version,uint32_t reserved,uint32_t handle){
 const uint32_t supported=NORM_IGNORECASE|NORM_IGNORENONSPACE|NORM_IGNORESYMBOLS|SORT_STRINGSORT|NORM_IGNOREKANATYPE|NORM_IGNOREWIDTH|NORM_LINGUISTIC_CASING|LINGUISTIC_IGNORECASE|LINGUISTIC_IGNOREDIACRITIC|SORT_DIGITSASNUMBERS|0x10000000|LOCALE_USE_CP_ACP;
 if(flags&~supported){tribes_set_last_error(1004);return 0;}
 if(version||reserved||handle)collation_unsupported("optional version/reserved/sort handle");
 locale_profile(base,locale);uint32_t alen=0,blen=0;uint16_t *acopy=copy_input(base,a,na,&alen),*bcopy=copy_input(base,b,nb,&blen);
 if(!acopy||!bcopy){free(acopy);free(bcopy);tribes_set_last_error(87);return 0;}
 int ret=compare_string(&default_sort,flags,acopy,(int)alen,bcopy,(int)blen);free(acopy);free(bcopy);return ret<0?1:ret>0?3:2;
}
