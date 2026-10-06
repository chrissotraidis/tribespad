/* Read-only volatile HKLM hardware registry for native CPU metadata.
 * UTF-16 names address the finite ASCII hardware namespace below. Handles
 * belong to a guest address space, carry rights, and are never reused.
 * ProcessorNameString comes from machdep.cpu.brand_string. ~MHz is present
 * uses hw.cpufrequency when available, otherwise the declared libuv Apple
 * compatibility fallback. That fallback is not measured physical CPU speed.
 * Other registry namespaces stop until their persistence/profile exists.
 */
#include "guest_registry.h"
#include "cpu_metadata_profile.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>
#define HKCU 0x80000001u
#define HKLM 0x80000002u
#define KEY_QUERY_VALUE 1u
#define KEY_READ 0x20019u
#define PATH_BYTES 256
static const char cpu_path[]="HARDWARE\\DESCRIPTION\\SYSTEM\\CENTRALPROCESSOR";
typedef struct Key {uint64_t base;uint32_t token,access;char path[PATH_BYTES];struct Key *next;} Key;
static Key *keys;
static uint32_t next_token=0xe5000001u;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static int range(uint64_t base,uint32_t p,uint64_t n){return base&&p&&n&&(uint64_t)p+n<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-((uint64_t)p+n-1);}
static void put32(uint64_t base,uint32_t p,uint32_t v){memcpy((void *)(uintptr_t)(base+p),&v,4);}
static int wide_name(uint64_t base,uint32_t p,char *out,size_t capacity){
 if(!p){out[0]=0;return 0;}
 for(size_t i=0;i<capacity;i++){
  if(!range(base,p,2*(i+1)))return 87;
  uint16_t u;memcpy(&u,(void *)(uintptr_t)(base+p+2*i),2);
  if(u>127){fputs("TribesPad: non-ASCII registry namespace not implemented\n",stderr);abort();}
  out[i]=(char)(u>='a'&&u<='z'?u-'a'+'A':u);if(!u)return 0;
 }
 return 206;
}
static int ansi_name(uint64_t base,uint32_t p,char *out,size_t capacity){
 if(!p){out[0]=0;return 0;}
 for(size_t i=0;i<capacity;i++){
  if(!range(base,p,i+1))return 87;
  unsigned char u;memcpy(&u,(void *)(uintptr_t)(base+p+i),1);
  if(u>127){fputs("TribesPad: non-ASCII registry namespace not implemented\n",stderr);abort();}
  out[i]=(char)(u>='a'&&u<='z'?u-'a'+'A':u);if(!u)return 0;
 }
 return 206;
}
/* The Apple game profile has no Windows accessibility registry settings.
 * Report the absent key, leaving the original client's default path intact.
 * This does not synthesize a Windows cursor size from unrelated Mac settings. */
static int absent_user_path(const char *path){
 static const char prefix[]="SOFTWARE\\MICROSOFT\\ACCESSIBILITY";
 size_t n=sizeof prefix-1;return !strncmp(path,prefix,n)&&(!path[n]||path[n]=='\\');
}
/* The original QoL font loader checks these two leaves in HKCU then HKLM.
 * This file-based Apple profile has neither Windows font registrations nor
 * font-substitution settings. Preserve its real missing-key fallback; do not
 * synthesize Windows registrations from Apple fonts or allow other software
 * keys to silently succeed/fail as if implemented. */
static int absent_font_path(const char *path){
 static const char *leaves[]={"SOFTWARE\\MICROSOFT\\WINDOWS NT\\CURRENTVERSION\\FONTS","SOFTWARE\\MICROSOFT\\WINDOWS NT\\CURRENTVERSION\\FONTSUBSTITUTES"};
 size_t n=strlen(path);while(n&&path[n-1]=='\\')n--;
 for(unsigned i=0;i<sizeof leaves/sizeof *leaves;i++)if(n==strlen(leaves[i])&&!memcmp(path,leaves[i],n))return 1;
 return 0;
}
static Key *find(uint64_t base,uint32_t token){for(Key *k=keys;k;k=k->next)if(k->base==base&&k->token==token)return k;return NULL;}
static unsigned cpu_count(void){long n=sysconf(_SC_NPROCESSORS_ONLN);if(n<1){fputs("TribesPad: registry CPU count unavailable\n",stderr);abort();}return n>32?32:(unsigned)n;}
/* Returns -1 for a parent, a CPU index for a leaf, -2 for absent profile keys.
 * No assumptions about unimplemented software/profile registry namespaces. */
static int path_kind(const char *path){
 if(!*path)return -1;
 /* This file-based Apple installation has no Windows installer registry key.
    The original EULA-region query at0x5c3ca0 takes its normal missing-key
    fallback. No region, license acceptance or installer data is invented. */
 if(!strcmp(path,"SOFTWARE\\SIERRA ON-LINE\\TRIBES2"))return -2;
 if(absent_font_path(path))return -2;
 const char *parents[]={"HARDWARE","HARDWARE\\DESCRIPTION","HARDWARE\\DESCRIPTION\\SYSTEM",cpu_path};
 for(unsigned i=0;i<sizeof parents/sizeof *parents;i++)if(!strcmp(path,parents[i]))return -1;
 if(strncmp(path,"HARDWARE\\",9)){fputs("TribesPad: registry namespace not implemented\n",stderr);abort();}
 size_t prefix=sizeof cpu_path-1;if(strncmp(path,cpu_path,prefix)||path[prefix]!='\\')return -2;
 const char *p=path+prefix+1;if(!*p||(*p=='0'&&p[1]))return -2;
 unsigned index=0;for(;*p;p++){if(*p<'0'||*p>'9'||index>31)return -2;index=index*10+(unsigned)(*p-'0');}
 return index<cpu_count()?(int)index:-2;
}
static uint32_t open_path(uint64_t base,uint32_t key,const char *sub,uint32_t options,uint32_t access,uint32_t *output){
 if(options){fputs("TribesPad: registry open options not implemented\n",stderr);abort();}
 /* Hardware is a shared read-only view; select either WOW64 view, not both. */
 if((access&0x300u)==0x300u)return 87;
 if(access&~(KEY_READ|0x300u))return 5;
 char path[PATH_BYTES];
 if(key==HKCU){
  if(!*sub){*output=HKCU;return 0;}
  if(absent_user_path(sub)||absent_font_path(sub))return 2;
  fputs("TribesPad: user registry namespace not implemented\n",stderr);abort();
 }
 pthread_mutex_lock(&mutex);Key *parent=find(base,key);
 if(key!=HKLM&&!parent){pthread_mutex_unlock(&mutex);return 6;}
 const char *prefix=parent?parent->path:"";size_t a=strlen(prefix),b=strlen(sub);
 if(a+b+(a&&b?1:0)>=sizeof path){pthread_mutex_unlock(&mutex);return 206;}
 memcpy(path,prefix,a);size_t at=a;if(a&&b)path[at++]='\\';memcpy(path+at,sub,b+1);
 size_t total=at+b;while(total&&path[total-1]=='\\')path[--total]=0;
 if(key==HKLM&&!*path){pthread_mutex_unlock(&mutex);*output=HKLM;return 0;}
 if(path_kind(path)==-2){pthread_mutex_unlock(&mutex);return 2;}
 Key *entry=calloc(1,sizeof *entry);if(!entry||next_token>=0xe6000000u){free(entry);pthread_mutex_unlock(&mutex);return 8;}
 entry->base=base;entry->token=next_token++;entry->access=access&~0x300u;strcpy(entry->path,path);entry->next=keys;keys=entry;
 uint32_t token=entry->token;pthread_mutex_unlock(&mutex);*output=token;return 0;
}
static uint32_t open_guest(uint64_t base,uint32_t key,uint32_t subkey,uint32_t options,uint32_t access,uint32_t output,int ansi){
 if(!range(base,output,4))return 87;
 char sub[PATH_BYTES];int error=ansi?ansi_name(base,subkey,sub,sizeof sub):wide_name(base,subkey,sub,sizeof sub);if(error)return (uint32_t)error;
 uint32_t handle;uint32_t status=open_path(base,key,sub,options,access,&handle);
 if(!status)put32(base,output,handle);return status;
}
uint32_t tribes_reg_open_key_ex_w(uint64_t b,uint32_t k,uint32_t s,uint32_t o,uint32_t a,uint32_t r){return open_guest(b,k,s,o,a,r,0);}
uint32_t tribes_reg_open_key_ex_a(uint64_t b,uint32_t k,uint32_t s,uint32_t o,uint32_t a,uint32_t r){return open_guest(b,k,s,o,a,r,1);}
uint32_t tribes_reg_close_key(uint64_t base,uint32_t token){
 if(token==HKLM||token==HKCU)return 0;
 pthread_mutex_lock(&mutex);Key **p=&keys;while(*p&&((*p)->base!=base||(*p)->token!=token))p=&(*p)->next;
 if(!*p){pthread_mutex_unlock(&mutex);return 6;}Key *old=*p;*p=old->next;pthread_mutex_unlock(&mutex);free(old);return 0;
}
static uint32_t value(const char *name,unsigned char bytes[512],uint32_t *type,uint32_t *length){
 if(!strcmp(name,"PROCESSORNAMESTRING")){
  char brand[256];size_t n=sizeof brand;
  if(sysctlbyname("machdep.cpu.brand_string",brand,&n,NULL,0)){return errno==ENOENT?2:31;}
  if(!n||n>sizeof brand||brand[n-1])return 13;
  for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)brand[i];if(c>127)return 13;bytes[2*i]=c;bytes[2*i+1]=0;}
  *type=1;*length=(uint32_t)(n*2);return 0;
 }
 if(!strcmp(name,"~MHZ")){
  uint64_t hz=0;size_t n=sizeof hz;
  int native=sysctlbyname("hw.cpufrequency",&hz,&n,NULL,0);
  if(native&&errno!=ENOENT)return 31;
  if(!native&&n&&n!=sizeof hz)return 13;
  if(native||!n||!hz)hz=UINT64_C(1000000)*TRIBES_CPU_COMPATIBILITY_MHZ;
  uint64_t mhz=hz/1000000;if(!mhz||mhz>UINT32_MAX)return 13;
  uint32_t v=(uint32_t)mhz;memcpy(bytes,&v,4);*type=4;*length=4;return 0;
 }
 return 2;
}
uint32_t tribes_reg_query_value_ex_w(uint64_t base,uint32_t key,uint32_t name,uint32_t reserved,uint32_t type,uint32_t data,uint32_t size){
 if(reserved||(data&&!size)||(type&&!range(base,type,4))||(size&&!range(base,size,4)))return 87;
 char field[PATH_BYTES];int error=wide_name(base,name,field,sizeof field);if(error)return (uint32_t)error;
 pthread_mutex_lock(&mutex);Key *entry=find(base,key);
 if(!entry){pthread_mutex_unlock(&mutex);return key==HKLM?2:6;}
 if(!(entry->access&KEY_QUERY_VALUE)){pthread_mutex_unlock(&mutex);return 5;}
 int kind=path_kind(entry->path);pthread_mutex_unlock(&mutex);if(kind<0)return 2;
 unsigned char bytes[512];uint32_t vtype,length;uint32_t status=value(field,bytes,&vtype,&length);if(status)return status;
 uint32_t capacity=0;if(data)memcpy(&capacity,(void *)(uintptr_t)(base+size),4);
 if(data&&capacity>=length&&!range(base,data,length))return 87;
 if(type)put32(base,type,vtype);
 if(size)put32(base,size,length);
 if(!data)return 0;
 if(capacity<length)return 234;
 memcpy((void *)(uintptr_t)(base+data),bytes,length);return 0;
}

/* RegGetValueA for the same read-only backing. Stored CPU strings are ASCII
 * UTF-16, so ANSI conversion is exact; no expandable values exist here. */
uint32_t tribes_reg_get_value_a(uint64_t base,uint32_t key,uint32_t subkey,uint32_t name,uint32_t flags,uint32_t type,uint32_t data,uint32_t size){
 if((data&&!size)||(type&&!range(base,type,4))||(size&&!range(base,size,4)))return 87;
 uint32_t capacity=0;if(data){memcpy(&capacity,(void *)(uintptr_t)(base+size),4);if(capacity&&!range(base,data,capacity))return 87;}
 uint32_t status=0,handle=key,length=0,vtype=0;unsigned char bytes[512];int opened=0;
 if((flags&~0x3003ffffu)||(flags&0x30000u)==0x30000u){status=87;goto done;}
 char sub[PATH_BYTES],field[PATH_BYTES];status=ansi_name(base,subkey,sub,sizeof sub);if(status)goto done;
 status=ansi_name(base,name,field,sizeof field);if(status)goto done;
 if(*sub){status=open_path(base,key,sub,0,KEY_QUERY_VALUE,&handle);if(status)goto done;opened=handle!=HKCU&&handle!=HKLM;}
 pthread_mutex_lock(&mutex);Key *entry=find(base,handle);
 if(!entry){pthread_mutex_unlock(&mutex);status=handle==HKLM||handle==HKCU?2:6;goto done;}
 if(!(entry->access&KEY_QUERY_VALUE)){pthread_mutex_unlock(&mutex);status=5;goto done;}
 int kind=path_kind(entry->path);pthread_mutex_unlock(&mutex);if(kind<0){status=2;goto done;}
 status=value(field,bytes,&vtype,&length);if(status)goto done;
 if(vtype==1){length/=2;for(uint32_t i=0;i<length;i++)bytes[i]=bytes[2*i];}
 if(type)put32(base,type,vtype);if(size)put32(base,size,length);
 uint32_t mask=flags&0xffffu,bit=vtype==1?2u:16u;
 if(mask&&mask!=0xffffu&&!(mask&bit)){status=1630;goto done;}
 if(data&&capacity<length){status=234;goto done;}
 if(data)memcpy((void *)(uintptr_t)(base+data),bytes,length);
 done:
 if(opened)tribes_reg_close_key(base,handle);
 if(status&&(flags&0x20000000u)&&data&&capacity)memset((void *)(uintptr_t)(base+data),0,capacity);
 return status;
}
