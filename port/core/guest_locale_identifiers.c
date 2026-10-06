/* Immutable Wine 10.0 locale index semantics; LGPL-2.1-or-later.
   Original source notices: vendor/wine-nls/LOCALE-INFO-NOTICE and LICENSE.
   User/system defaults are the declared en-US runtime profile, not host UI. */
#include "guest_locale_identifiers.h"
#include "guest_heap.h"
#include <string.h>
struct LocaleName {const char *name;uint32_t lcid,default_lcid,neutral;};
struct LocaleID {uint32_t lcid;const char *canonical,*specific;};
#include "nls_identifiers.inc"
static int range(uint64_t base,uint32_t p,uint64_t n){return p>=0x10000&&(uint64_t)p+n<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-p-n;}
static unsigned normalize(unsigned c){if(c>='a'&&c<='z')return c-'a'+'A';return c=='_'?'-':c;}
static int equal(const char *a,const char *b){for(;;a++,b++){if(normalize((unsigned char)*a)!=normalize((unsigned char)*b))return 0;if(!*a)return 1;}}
static const struct LocaleName *find(uint64_t base,uint32_t p,int defaults){
 char name[85];if(!p){if(!defaults)return NULL;strcpy(name,"en-US");}
 else{unsigned i;for(i=0;i<85;i++){if(!range(base,p,2ull*(i+1)))return NULL;uint16_t c;memcpy(&c,(void *)(uintptr_t)(base+p+2ull*i),2);if(c>127)return NULL;name[i]=(char)c;if(!c)break;}if(i==85)return NULL;}
 if(defaults&&equal(name,"!x-sys-default-locale"))strcpy(name,"en-US");
 for(unsigned i=0;i<sizeof locale_names/sizeof locale_names[0];i++)if(equal(name,locale_names[i].name))return locale_names+i;return NULL;
}
static uint32_t output_name(uint64_t base,const char *name,uint32_t output,uint32_t capacity){
 if((int32_t)capacity<0||(capacity&&!output)){tribes_set_last_error(87);return 0;}
 unsigned n=(unsigned)strlen(name)+1;if(!capacity)return n;if(capacity<n){tribes_set_last_error(122);return 0;}if(!range(base,output,2ull*n)){tribes_set_last_error(87);return 0;}
 for(unsigned i=0;i<n;i++){uint16_t c=(unsigned char)name[i];memcpy((void *)(uintptr_t)(base+output+2ull*i),&c,2);}return n;
}
uint32_t tribes_get_user_default_locale_name(uint64_t base,uint32_t output,uint32_t capacity){return output_name(base,"en-US",output,capacity);}
uint32_t tribes_is_valid_locale_name(uint64_t base,uint32_t name){return find(base,name,0)!=NULL;}
uint32_t tribes_locale_name_to_lcid(uint64_t base,uint32_t name,uint32_t flags){
 if(flags&~0x08000000u){tribes_set_last_error(87);return 0;}const struct LocaleName *n=find(base,name,1);if(!n){tribes_set_last_error(87);return 0;}return !(flags&0x08000000u)&&n->neutral?n->default_lcid:n->lcid;
}
uint32_t tribes_lcid_to_locale_name(uint64_t base,uint32_t lcid,uint32_t output,uint32_t capacity,uint32_t flags){
 if(flags&~0x08000000u){tribes_set_last_error(87);return 0;}
 switch(lcid){case 0:case 0x400:case 0x800:case 0xc00:case 0x1000:case 0x1400:return output_name(base,"en-US",output,capacity);}
 for(unsigned i=0;i<sizeof locale_ids/sizeof locale_ids[0];i++)if(locale_ids[i].lcid==lcid)return output_name(base,flags&0x08000000u?locale_ids[i].canonical:locale_ids[i].specific,output,capacity);
 tribes_set_last_error(87);return 0;
}
