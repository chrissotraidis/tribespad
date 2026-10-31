/* version.dll for Tribes: Vengeance: GetFileVersionInfoSize, GetFileVersionInfo
 * and VerQueryValue (A and W), from the VS_VERSIONINFO resource of a file in the
 * mounted namespace. The block is the file's own UTF-16 resource; the A forms
 * keep room after it for ANSI copies of string values, as Windows does.
 * https://learn.microsoft.com/en-us/windows/win32/api/winver/nf-winver-verqueryvaluew
 * https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../core/guest_filesystem.h"

extern uint32_t tribes_set_last_error(uint32_t value);
#define GUEST(base,address) ((void *)(uintptr_t)((base)+(address)))
static int in_guest(uint64_t base,uint32_t address,uint32_t bytes){return address&&address<=UINT32_MAX-bytes&&base<=UINTPTR_MAX-address-bytes;}
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
static uint32_t u32(const uint8_t *p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}

static int read_name(uint64_t base,uint32_t name,int wide,char *out,size_t capacity){
 if(!name){tribes_set_last_error(87);return 0;}
 for(size_t i=0;i<capacity;i++){
  uint32_t at=name+(uint32_t)(i*(wide?2:1));if(!in_guest(base,at,2)){tribes_set_last_error(87);return 0;}
  uint16_t v;if(wide)memcpy(&v,GUEST(base,at),2);else v=*(const unsigned char *)GUEST(base,at);
  if(v>127){tribes_set_last_error(50);return 0;}out[i]=(char)v;if(!v)return 1;
 }
 tribes_set_last_error(206);return 0;
}
/* The file's RT_VERSION resource (first name, first language), malloc'd. */
static uint8_t *version_resource(uint64_t base,uint32_t name,int wide,uint32_t *size){
 char guest[PATH_MAX],host[PATH_MAX];if(!read_name(base,name,wide,guest,sizeof guest))return NULL;
 if(!tribes_filesystem_resolve(base,guest,host,sizeof host)){tribes_set_last_error(2);return NULL;}
 FILE *f=fopen(host,"rb");if(!f){tribes_set_last_error(2);return NULL;}
 fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
 uint8_t *file=n>64&&n<(1<<28)?malloc((size_t)n):NULL;
 if(!file||fread(file,1,(size_t)n,f)!=(size_t)n){fclose(f);free(file);tribes_set_last_error(1812);return NULL;}fclose(f);
 uint8_t *result=NULL;uint32_t pe=u32(file+0x3c);
 if(pe+0xf8>(uint32_t)n||memcmp(file+pe,"PE\0\0",4))goto done;
 uint16_t sections=u16(file+pe+6),optional=u16(file+pe+20);uint32_t table=pe+24+optional;
 if(optional<0x78)goto done;
 uint32_t rsrc=u32(file+pe+24+0x70),rsize=u32(file+pe+24+0x74);if(!rsrc||!rsize)goto done;
 #define OFFSET(rva,out) do{out=0;for(uint16_t s=0;s<sections;s++){const uint8_t *h=file+table+40u*s;uint32_t va=u32(h+12),vs=u32(h+8),raw=u32(h+20),rs=u32(h+16);if((rva)>=va&&(rva)<va+(vs>rs?vs:rs)){out=raw+(rva)-va;break;}}}while(0)
 uint32_t root;OFFSET(rsrc,root);if(!root||root+16>(uint32_t)n)goto done;
 /* Three directory levels: type 16, then the first entry, then the first entry. */
 uint32_t dir=root;int want=16;
 for(int level=0;level<3;level++){
  uint16_t named=u16(file+dir+12),ids=u16(file+dir+14);uint32_t found=0;
  for(uint32_t i=0;i<(uint32_t)named+ids;i++){const uint8_t *e=file+dir+16+8*i;if(dir+16+8*i+8>(uint32_t)n)goto done;
   if(want<0||(!(u32(e)&0x80000000u)&&(int)u32(e)==want)){found=u32(e+4);break;}}
  if(!found)goto done;want=-1;
  if(level<2){if(!(found&0x80000000u))goto done;dir=root+(found&0x7fffffffu);if(dir+16>(uint32_t)n)goto done;}
  else{if(found&0x80000000u)goto done;uint32_t entry=root+found;if(entry+16>(uint32_t)n)goto done;
   uint32_t data_rva=u32(file+entry),data_size=u32(file+entry+4),at;OFFSET(data_rva,at);
   if(!at||at+data_size>(uint32_t)n||data_size<6)goto done;
   result=malloc(data_size);if(result){memcpy(result,file+at,data_size);*size=data_size;}}
 }
done:
 free(file);if(!result)tribes_set_last_error(1813);return result;
}
static uint32_t info_size(uint64_t base,uint32_t name,uint32_t handle,int wide){
 if(handle&&in_guest(base,handle,4)){uint32_t zero=0;memcpy(GUEST(base,handle),&zero,4);}
 uint32_t size;uint8_t *block=version_resource(base,name,wide,&size);if(!block)return 0;free(block);
 return wide?size:2*size; /* A: room for ANSI copies of string values */
}
static uint32_t info(uint64_t base,uint32_t name,uint32_t capacity,uint32_t output,int wide){
 uint32_t size;uint8_t *block=version_resource(base,name,wide,&size);if(!block)return 0;
 uint32_t total=wide?size:2*size;if(capacity<total)total=capacity;
 if(!in_guest(base,output,total)){free(block);tribes_set_last_error(87);return 0;}
 memset(GUEST(base,output),0,total);memcpy(GUEST(base,output),block,size<total?size:total);free(block);return 1;
}
uint32_t vg_get_file_version_info_size_a(uint64_t base,uint32_t name,uint32_t handle){return info_size(base,name,handle,0);}
uint32_t vg_get_file_version_info_size_w(uint64_t base,uint32_t name,uint32_t handle){return info_size(base,name,handle,1);}
uint32_t vg_get_file_version_info_a(uint64_t base,uint32_t name,uint32_t handle,uint32_t capacity,uint32_t output){(void)handle;return info(base,name,capacity,output,0);}
uint32_t vg_get_file_version_info_w(uint64_t base,uint32_t name,uint32_t handle,uint32_t capacity,uint32_t output){(void)handle;return info(base,name,capacity,output,1);}

/* VerQueryValue: walk "\\Key\\Key..." from the VS_VERSIONINFO root. */
static uint32_t query(uint64_t base,uint32_t block,uint32_t path,uint32_t out_pointer,uint32_t out_length,int wide){
 char want[256];if(!read_name(base,path,wide,want,sizeof want))return 0;
 if(!in_guest(base,block,6)||!in_guest(base,out_pointer,4)||!in_guest(base,out_length,4)){tribes_set_last_error(87);return 0;}
 const uint8_t *b=GUEST(base,block);uint32_t total=u16(b);if(!in_guest(base,block,total)){tribes_set_last_error(87);return 0;}
 uint32_t node=0;char *save=NULL;
 for(char *part=strtok_r(want,"\\",&save);part;part=strtok_r(NULL,"\\",&save)){
  uint32_t length=u16(b+node),key=node+6,k=key;while(k+1<total&&u16(b+k))k+=2;
  uint32_t value=(k+2+3)&~3u,vlen=u16(b+node+2)*(u16(b+node+4)==1?2u:1u),child=(value+vlen+3)&~3u,found=0;
  while(child+6<=node+length&&child<total){
   uint32_t clen=u16(b+child);if(!clen)break;
   char name[128];size_t i=0;for(uint32_t c=child+6;c+1<total&&u16(b+c)&&i<sizeof name-1;c+=2)name[i++]=(char)u16(b+c);name[i]=0;
   if(!strcasecmp(name,part)){found=child;break;}
   child=(child+clen+3)&~3u;
  }
  if(!found){tribes_set_last_error(1813);return 0;}
  node=found;
 }
 uint32_t key=node+6,k=key;while(k+1<total&&u16(b+k))k+=2;
 uint32_t value=(k+2+3)&~3u,type=u16(b+node+4),count=u16(b+node+2);
 if(!count){tribes_set_last_error(1813);return 0;}
 uint32_t address=block+value,length=count;
 if(type==1&&!wide){
  /* ANSI copy in the block's second half, at a place unique to this value. */
  uint32_t copy=block+total+value/2;char *dest=GUEST(base,copy);
  for(uint32_t i=0;i<count;i++){uint16_t v=u16(b+value+2*i);dest[i]=(char)(v<128?v:'?');}
  address=copy;
 }
 memcpy(GUEST(base,out_pointer),&address,4);memcpy(GUEST(base,out_length),&length,4);return 1;
}
uint32_t vg_ver_query_value_a(uint64_t base,uint32_t block,uint32_t path,uint32_t out_pointer,uint32_t out_length){return query(base,block,path,out_pointer,out_length,0);}
uint32_t vg_ver_query_value_w(uint64_t base,uint32_t block,uint32_t path,uint32_t out_pointer,uint32_t out_length){return query(base,block,path,out_pointer,out_length,1);}
