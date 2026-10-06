/* Pinned Wine NLS presentation data and date/time pattern semantics.
   Original reader: Wine 10.0 dlls/kernelbase/locale.c (LGPL-2.1-or-later).
   See vendor/wine-nls/LICENSE and config/encoding-sources.json.
   Explicit Gregorian/no-registry-override profile; no host locale formatter. */
#include "guest_datetime.h"
#include "guest_heap.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
struct DateLocale {uint32_t calendar,formats[5],days[2][7],months[4][12],meridian[2],era[2];};
struct DateName {const char *name;uint32_t index;};
#include "nls_datetime.inc"
typedef struct {uint16_t year,month,dow,day,hour,minute,second,millisecond;} GuestTime;
_Static_assert(sizeof(GuestTime)==16,"x86 SYSTEMTIME");
static _Noreturn void unsupported(const char *why){fprintf(stderr,"TribesPad: date/time formatting unsupported: %s\n",why);abort();}
static int range(uint64_t base,uint32_t p,uint64_t n){return p>=0x10000&&(uint64_t)p+n<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-p-n;}
static uint16_t unit(uint64_t base,uint32_t p,unsigned i){uint16_t v;memcpy(&v,(void *)(uintptr_t)(base+p+2ull*i),2);return v;}
static uint16_t *guest_string(uint64_t base,uint32_t p){
 unsigned n;for(n=0;n<4096;n++){if(!range(base,p,2ull*(n+1)))return NULL;if(!unit(base,p,n))break;}
 if(n==4096)unsupported("format/name exceeds bounded profile");uint16_t *s=malloc(2ull*(n+1));if(!s)unsupported("native input allocation");memcpy(s,(void *)(uintptr_t)(base+p),2ull*(n+1));return s;
}
static const struct DateLocale *locale(uint64_t base,uint32_t p){
 char name[86]="en-US";if(p){unsigned i;for(i=0;i<85;i++){if(!range(base,p,2ull*(i+1)))return NULL;uint16_t ch=unit(base,p,i);if(ch>127)return NULL;name[i]=(char)ch;if(!ch)break;}if(i==85)return NULL;}
 if(!strcasecmp(name,"!x-sys-default-locale"))strcpy(name,"en-US");
 for(unsigned i=0;i<sizeof dt_names/sizeof dt_names[0];i++)if(!strcasecmp(name,dt_names[i].name))return dt_locales+dt_names[i].index;return NULL;
}
static int get_time(uint64_t base,uint32_t p,GuestTime *t,int date){
 if(p){if(!range(base,p,16))return 0;memcpy(t,(void *)(uintptr_t)(base+p),16);}
 else{struct timespec now;struct tm tm;if(clock_gettime(CLOCK_REALTIME,&now)||!localtime_r(&now.tv_sec,&tm))unsupported("native local clock");*t=(GuestTime){(uint16_t)(tm.tm_year+1900),(uint16_t)(tm.tm_mon+1),(uint16_t)tm.tm_wday,(uint16_t)tm.tm_mday,(uint16_t)tm.tm_hour,(uint16_t)tm.tm_min,(uint16_t)tm.tm_sec,(uint16_t)(now.tv_nsec/1000000)};}
 if(!date)return t->hour<24&&t->minute<60&&t->second<60&&t->millisecond<1000;
 if(t->year<1601||t->year>30827||!t->month||t->month>12||!t->day)return 0;
 static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};unsigned leap=t->year%4==0&&(t->year%100||t->year%400==0);if(t->day>days[t->month-1]+(t->month==2?leap:0))return 0;
 unsigned y=t->year,m=t->month;if(m<3){m+=12;y--;}t->dow=(t->day+13*(m+1)/5+y+y/4-y/100+y/400+6)%7;return 1;
}
struct Text {uint16_t *s;unsigned n,capacity;};
static void append(struct Text *b,uint16_t ch){if(b->n==b->capacity){if(b->capacity==1048576)unsupported("expanded output exceeds bounded profile");unsigned capacity=b->capacity*2;uint16_t *s=realloc(b->s,2ull*capacity);if(!s)unsupported("native output allocation");b->s=s;b->capacity=capacity;}b->s[b->n++]=ch;}
static void text(struct Text *b,uint32_t offset){const uint16_t *s=dt_strings+offset;while(*s)append(b,*s++);}
static void number(struct Text *b,unsigned value,unsigned width){char s[16];int n=snprintf(s,sizeof s,"%0*u",(int)width,value);if(n<0||(unsigned)n>=sizeof s)unsupported("numeric output bound");for(int i=0;i<n;i++)append(b,(unsigned char)s[i]);}
/* Quote handling follows the pinned Wine reader, including doubled embedded
   quotes. Date era token grouping also accepts g per Microsoft's format docs. */
static unsigned pattern_length(const uint16_t *p,int date){unsigned i;if(*p=='\''){for(i=1;p[i];i++){if(p[i]!='\'')continue;if(p[++i]!='\'')return i;}return i;}int accepted=date?(*p=='y'||*p=='M'||*p=='d'||*p=='g'):(*p=='H'||*p=='h'||*p=='m'||*p=='s'||*p=='t');if(!accepted)return 1;for(i=1;p[i]==p[0];i++){}return i;}
static uint32_t finish(uint64_t base,uint32_t output,uint32_t capacity,struct Text *b){append(b,0);uint32_t result=b->n;if(capacity){if(!range(base,output,2ull*capacity)){free(b->s);tribes_set_last_error(87);return 0;}unsigned n=b->n<capacity?b->n:capacity;memcpy((void *)(uintptr_t)(base+output),b->s,2ull*n);if(result>capacity){uint16_t zero=0;memcpy((void *)(uintptr_t)(base+output+2ull*(capacity-1)),&zero,2);result=0;tribes_set_last_error(122);}}free(b->s);return result;}
static struct Text make_text(void){struct Text b={malloc(2u*256),0,256};if(!b.s)unsupported("native output allocation");return b;}
uint32_t tribes_get_date_format_ex(uint64_t base,uint32_t lp,uint32_t flags,uint32_t tp,uint32_t fp,uint32_t output,uint32_t capacity,uint32_t calendar){
 if((int32_t)capacity<0||(capacity&&!output)||calendar){tribes_set_last_error(87);return 0;}if(flags&~0xc00000ffu){tribes_set_last_error(1004);return 0;}if(flags&0x74)unsupported("alternate calendar/reading layout mode");
 const struct DateLocale *l=locale(base,lp);if(!l){tribes_set_last_error(87);return 0;}if(l->calendar!=1)unsupported("non-Gregorian default locale calendar");
 unsigned mode=flags&0x8b;if(fp&&(mode||(flags&0x80000000))){tribes_set_last_error(1004);return 0;}unsigned which=0;if(!fp){if(!mode||mode==1)which=0;else if(mode==2)which=1;else if(mode==8)which=2;else if(mode==128)which=3;else{tribes_set_last_error(1004);return 0;}}
 GuestTime t;if(!get_time(base,tp,&t,1)){tribes_set_last_error(87);return 0;}uint16_t *custom=fp?guest_string(base,fp):NULL;if(fp&&!custom){tribes_set_last_error(87);return 0;}const uint16_t *f=fp?custom:dt_strings+l->formats[which];struct Text b=make_text();unsigned genitive=0;
 for(;*f;){unsigned n=pattern_length(f,1);switch(*f){
 case '\'':for(unsigned i=1;i<n;i++){if(f[i]=='\'')i++;if(i<n)append(&b,f[i]);}break;
 case 'y':number(&b,n==1?t.year%10:n==2?t.year%100:t.year,n==1?1:2);break;
 case 'M':if(n<=2){number(&b,t.month,n);break;}if(!genitive){for(unsigned i=n;f[i];i+=pattern_length(f+i,1)){if(f[i]!='d')continue;if(f[i+1]!='d'||f[i+2]!='d')genitive=1;break;}}text(&b,l->months[(n==3?1:0)+(genitive?2:0)][t.month-1]);break;
 case 'd':if(n<=2){genitive=1;number(&b,t.day,n);}else{genitive=0;text(&b,l->days[n==3?1:0][t.dow]);}break;
 case 'g':text(&b,l->era[0]);break;
 default:append(&b,*f);break;}
 f+=n;}free(custom);return finish(base,output,capacity,&b);
}
uint32_t tribes_get_time_format_ex(uint64_t base,uint32_t lp,uint32_t flags,uint32_t tp,uint32_t fp,uint32_t output,uint32_t capacity){
 if((int32_t)capacity<0||(capacity&&!output)){tribes_set_last_error(87);return 0;}if(flags&~0xc000000fu){tribes_set_last_error(1004);return 0;}if(fp&&(flags&0x80000000)){tribes_set_last_error(1004);return 0;}
 const struct DateLocale *l=locale(base,lp);GuestTime t;if(!l||!get_time(base,tp,&t,0)){tribes_set_last_error(87);return 0;}uint16_t *custom=fp?guest_string(base,fp):NULL;if(fp&&!custom){tribes_set_last_error(87);return 0;}const uint16_t *f=fp?custom:dt_strings+l->formats[4];struct Text b=make_text();unsigned last=0;int skip=0;
 for(;*f;){unsigned n=pattern_length(f,0),val=0;switch(*f){
 case '\'':for(unsigned i=1;i<n;i++){if(f[i]=='\'')i++;if(!skip&&i<n)append(&b,f[i]);}break;
 case 'H':case 'h':val=t.hour;if(*f=='h'&&!(flags&8)){val%=12;if(!val)val=12;}number(&b,val,n<2?n:2);last=b.n;skip=0;break;
 case 'm':case 's':if((*f=='m'&&(flags&1))||(*f=='s'&&(flags&3))){b.n=last;skip=1;}else{number(&b,*f=='m'?t.minute:t.second,n<2?n:2);last=b.n;skip=0;}break;
 case 't':if(flags&4){b.n=last;skip=1;}else{uint32_t at=l->meridian[t.hour<12?0:1];if(n>1)text(&b,at);else append(&b,dt_strings[at]);skip=0;}break;
 default:if(!skip||*f==' ')append(&b,*f);break;}
 f+=n;}free(custom);return finish(base,output,capacity,&b);
}
