/* Case-independent Win32 image metadata in original guest addresses.
   https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlea
   https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamea */
#include "guest_modules.h"
#include "guest_heap.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
typedef struct Export { char name[256];uint16_t ordinal;uint32_t address; } Export;
typedef struct Resource {TribesImageResource info;int loaded;} Resource;
typedef struct Module { uint64_t base;uint32_t image,size;int main,native,pinned,pending;uint32_t references,count;Export *exports;Resource *resources;uint32_t resource_count;int resources_defined;char path[4096];struct Module *next; } Module;
static Module *modules;
typedef struct AbsentModule {uint64_t base;char name[4096];struct AbsentModule *next;} AbsentModule;
static AbsentModule *absent_modules;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int absent_leaf(uint64_t base,const char *path){
    const char *leaf=strrchr(path,'\\');leaf=leaf?leaf+1:path;
    for(AbsentModule *a=absent_modules;a;a=a->next)if(a->base==base&&!strcasecmp(a->name,leaf))return 1;
    return 0;
}
/* Host availability metadata, never a fabricated image/provider. The host must
   audit its DLL search namespace before declaring a bare name absent. Unknown
   providers and explicit path loads still stop; no PE loader is implied. */
int tribes_module_define_absent(uint64_t base,const char *name){
    if(!name){errno=EINVAL;return 0;}size_t n=strnlen(name,4096);
    if(!n||n+5>4096||name[n-1]=='.'||strpbrk(name,"\\/:*?\"<>|")){errno=EINVAL;return 0;}
    for(size_t i=0;i<n;i++)if((unsigned char)name[i]>=128){errno=EINVAL;return 0;}
    AbsentModule *a=calloc(1,sizeof *a);if(!a)return 0;a->base=base;strcpy(a->name,name);
    if(!strchr(name,'.'))strcat(a->name,".dll");
    pthread_mutex_lock(&lock);
    for(Module *m=modules;m;m=m->next)if(m->base==base){const char *leaf=strrchr(m->path,'\\');leaf=leaf?leaf+1:m->path;if(!strcasecmp(a->name,leaf)){pthread_mutex_unlock(&lock);free(a);errno=EALREADY;return 0;}}
    if(absent_leaf(base,a->name)){pthread_mutex_unlock(&lock);free(a);errno=EALREADY;return 0;}
    a->next=absent_modules;absent_modules=a;pthread_mutex_unlock(&lock);return 1;
}
static int overlaps(uint32_t a,uint32_t na,uint32_t b,uint32_t nb) {
    return (uint64_t)a<(uint64_t)b+(nb?nb:1)&&(uint64_t)b<(uint64_t)a+(na?na:1);
}
static int register_image(uint64_t base,uint32_t image,uint32_t size,const char *path,int main_image) {
    if(!image||(uint64_t)image+size>UINT64_C(0x100000000)||!path||strnlen(path,4096)==4096||strlen(path)<4||path[1]!=':'||path[2]!='\\'){errno=EINVAL;return 0;}
    Module *entry=calloc(1,sizeof *entry);if(!entry)return 0;entry->base=base;entry->image=image;entry->size=size;entry->references=1;entry->main=!!main_image;strcpy(entry->path,path);
    pthread_mutex_lock(&lock);if(absent_leaf(base,path)){pthread_mutex_unlock(&lock);free(entry);errno=EALREADY;return 0;}for(Module *m=modules;m;m=m->next)if(m->base==base&&(overlaps(image,size,m->image,m->size)||!strcasecmp(m->path,path)||(main_image&&m->main))){pthread_mutex_unlock(&lock);free(entry);errno=EALREADY;return 0;}
    entry->next=modules;modules=entry;pthread_mutex_unlock(&lock);return 1;
}
int tribes_module_register(uint64_t base,uint32_t image,const char *path,int main_image) {
    return register_image(base,image,0,path,main_image);
}
int tribes_module_register_image(uint64_t base,uint32_t image,uint32_t size,const char *path,int main_image) {
    if(!size){errno=EINVAL;return 0;}return register_image(base,image,size,path,main_image);
}
static int visible(Module *m) {return !m->pending&&(!m->native||m->references||m->pinned);}
/* Pending AOT images are mapped by the host but not yet DLL-initialized. */
int tribes_module_set_pending(uint64_t base,uint32_t image,int pending){
    int ok=0;pthread_mutex_lock(&lock);
    for(Module *m=modules;m;m=m->next)if(m->base==base&&m->image==image&&!m->native&&!m->main){m->pending=!!pending;ok=1;break;}
    pthread_mutex_unlock(&lock);return ok;
}
static Module *resource_module(uint64_t base,uint32_t image) {
    for(Module *m=modules;m;m=m->next)if(m->base==base&&(image?m->image==image:m->main)&&visible(m))return m;
    return NULL;
}
int tribes_module_register_resources(uint64_t base,uint32_t image,const TribesImageResource *rows,uint32_t count) {
    if(!image||count>65536||(count&&!rows)){errno=EINVAL;return 0;}
    Resource *copy=count?calloc(count,sizeof *copy):NULL;if(count&&!copy)return 0;
    pthread_mutex_lock(&lock);Module *m=resource_module(base,image);int error=0;
    if(!m||m->native||!m->size)error=EINVAL;
    else if(m->resources_defined)error=EALREADY;
    for(uint32_t i=0;!error&&i<count;i++){
        TribesImageResource r=rows[i];
        if(r.type>65535||r.name>65535||r.language>65535||!r.entry_rva||!r.data_rva||
           (uint64_t)r.entry_rva+16>m->size||(uint64_t)r.data_rva+r.size>m->size||
           r.data_rva>=m->size)error=EINVAL;
        for(uint32_t j=0;!error&&j<count;j++){
            if(overlaps(r.entry_rva,16,rows[j].data_rva,rows[j].size))error=EINVAL;
            if(j<i&&(r.entry_rva==rows[j].entry_rva||r.data_rva==rows[j].data_rva||
               (r.type==rows[j].type&&r.name==rows[j].name)))error=EINVAL;
        }
        copy[i].info=r;
    }
    if(!error){m->resources=copy;m->resource_count=count;m->resources_defined=1;}
    pthread_mutex_unlock(&lock);if(error){free(copy);errno=error;return 0;}return 1;
}
uint32_t tribes_find_resource_a(uint64_t base,uint32_t image,uint32_t name,uint32_t type) {
    /* String identifiers and language selection need a separate reached contract. */
    if(name>65535||type>65535){fprintf(stderr,"TribesPad: named FindResourceA is not implemented\n");abort();}
    uint32_t result=0,error=1813;pthread_mutex_lock(&lock);Module *m=resource_module(base,image);
    if(!m)error=6;
    else if(!m->resources_defined){pthread_mutex_unlock(&lock);fprintf(stderr,"TribesPad: module resource catalog unavailable\n");abort();}
    else if(!m->resource_count)error=1812;
    else for(uint32_t i=0;i<m->resource_count;i++){
        TribesImageResource *r=&m->resources[i].info;
        if(r->type==type){error=1814;if(r->name==name){result=m->image+r->entry_rva;break;}}
    }
    pthread_mutex_unlock(&lock);if(!result)tribes_set_last_error(error);return result;
}
static uint32_t resource_value(uint64_t base,uint32_t image,uint32_t handle,int load) {
    uint32_t result=0;int found=0;pthread_mutex_lock(&lock);Module *m=resource_module(base,image);
    if(m)for(uint32_t i=0;i<m->resource_count;i++){
        Resource *r=&m->resources[i];if(handle==m->image+r->info.entry_rva){
            found=1;if(load)r->loaded=1;result=load?m->image+r->info.data_rva:r->info.size;break;
        }
    }
    pthread_mutex_unlock(&lock);if(!found)tribes_set_last_error(6);return result;
}
uint32_t tribes_sizeof_resource(uint64_t base,uint32_t image,uint32_t handle){return resource_value(base,image,handle,0);}
uint32_t tribes_load_resource(uint64_t base,uint32_t image,uint32_t handle){return resource_value(base,image,handle,1);}
uint32_t tribes_lock_resource(uint64_t base,uint32_t handle){
    uint32_t result=0;pthread_mutex_lock(&lock);
    for(Module *m=modules;m&&!result;m=m->next)if(m->base==base&&visible(m))
        for(uint32_t i=0;i<m->resource_count;i++)if(m->resources[i].loaded&&handle==m->image+m->resources[i].info.data_rva){result=handle;break;}
    pthread_mutex_unlock(&lock);return result;
}
static int normalize_name(const char *guest,char *name,int *full) {
    size_t n=strnlen(guest,4096);if(n+5>4096){tribes_set_last_error(126);return 0;}
    memcpy(name,guest,n+1);
    for(size_t i=0;i<n;i++)if(name[i]=='/')name[i]='\\';
    *full=strchr(name,'\\')!=NULL||strchr(name,':')!=NULL;
    /* Absolute drive paths may contain current-directory components. Keep the
       complete directory identity; never fall back to a basename match. Other
       relative/parent/UNC resolution still needs the configured search policy. */
    if(n>=3&&((name[0]>='A'&&name[0]<='Z')||(name[0]>='a'&&name[0]<='z'))&&name[1]==':'&&name[2]=='\\'){
        size_t read=3,write=3;
        while(read<n){
            size_t end=read;while(end<n&&name[end]!='\\')end++;
            if(end-read==1&&name[read]=='.'){read=end+(end<n);continue;}
            while(read<end)name[write++]=name[read++];
            if(read<n){name[write++]='\\';read++;}
        }
        name[write]=0;n=write;
    }
    const char *leaf=strrchr(name,'\\');leaf=leaf?leaf+1:name;
    if(!*leaf){tribes_set_last_error(126);return 0;}
    if(!strchr(leaf,'.'))strcat(name,".dll");else if(n&&name[n-1]=='.')name[n-1]=0;
    return 1;
}
static Module *find_name(uint64_t base,const char *name,int full) {
    for(Module *m=modules;m;m=m->next)if(m->base==base&&visible(m)){
        const char *leaf=strrchr(m->path,'\\');leaf=leaf?leaf+1:m->path;
        if(!name?m->main:!strcasecmp(full?m->path:leaf,name))return m;
    }
    return NULL;
}
uint32_t tribes_module_pending_image(uint64_t base,const char *guest){
    char name[4096];int full=0;if(!guest||!normalize_name(guest,name,&full))return 0;
    uint32_t image=0;pthread_mutex_lock(&lock);
    for(Module *m=modules;m;m=m->next)if(m->base==base&&m->pending){
        const char *leaf=strrchr(m->path,'\\');leaf=leaf?leaf+1:m->path;
        if(!strcasecmp(full?m->path:leaf,name)){image=m->image;break;}
    }
    pthread_mutex_unlock(&lock);return image;
}
static uint32_t module_handle_name(uint64_t base,const char *guest) {
    char name[4096];int full=0;if(guest&&!normalize_name(guest,name,&full))return 0;
    pthread_mutex_lock(&lock);Module *m=find_name(base,guest?name:NULL,full);uint32_t result=m?m->image:0;
    pthread_mutex_unlock(&lock);if(!result)tribes_set_last_error(126);return result;
}
uint32_t tribes_get_module_handle(uint64_t base,uint32_t pointer) {
    return module_handle_name(base,pointer?(const char *)(uintptr_t)(base+pointer):NULL);
}
/* Wide lookup of the declared ASCII module namespace. It returns original
   image addresses and never adds a provider load reference. Non-ASCII names
   stop explicitly until Unicode path comparison is implemented. */
uint32_t tribes_get_module_handle_w(uint64_t base,uint32_t pointer) {
    if(!pointer)return module_handle_name(base,NULL);
    char name[4096];
    for(uint32_t i=0;i<sizeof name;i++) {
        uint64_t offset=(uint64_t)pointer+2ull*i;
        if(offset>UINT32_MAX-1||base>UINTPTR_MAX-offset-1){tribes_set_last_error(87);return 0;}
        uint16_t unit;memcpy(&unit,(void *)(uintptr_t)(base+offset),2);
        if(unit>127){fprintf(stderr,"TribesPad: unsupported non-ASCII GetModuleHandleW name\n");abort();}
        name[i]=(char)unit;
        if(!unit)return module_handle_name(base,name);
    }
    tribes_set_last_error(126);return 0;
}
/* Audited mapped-image bounds come from the host loader, never from mutable
   guest PE headers. Lookup plus retention/pinning is one catalog operation.
   Native providers support name lookup; their synthetic export addresses do
   not imply a mapped PE extent. Host-linked images retain one loader reference
   until detach/unmapping is implemented; additional references are balanced. */
static uint32_t module_handle_ex(uint64_t base,uint32_t flags,uint32_t pointer,uint32_t output,int wide) {
    if(!output||output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){tribes_set_last_error(87);return 0;}
    uint32_t value=0,error=0;char input[4096],name[4096];int full=0;
    if((flags&~7u)||((flags&3)==3)){error=87;goto failed;}
    if(pointer&&!(flags&4)){
        unsigned i;
        for(i=0;i<sizeof input;i++){
            uint64_t at=(uint64_t)pointer+(wide?2ull:1ull)*i;
            if(at>UINT32_MAX-(wide?1u:0u)||base>UINTPTR_MAX-at-(wide?1u:0u)){error=87;goto failed;}
            uint16_t unit=0;memcpy(&unit,(void *)(uintptr_t)(base+at),wide?2:1);
            if(unit>127){fprintf(stderr,"TribesPad: unsupported non-ASCII GetModuleHandleEx name\n");abort();}
            input[i]=(char)unit;if(!unit)break;
        }
        if(i==sizeof input){error=126;goto failed;}
        if(!normalize_name(input,name,&full)){error=tribes_get_last_error();goto failed;}
    }
    pthread_mutex_lock(&lock);Module *found=NULL;
    if(pointer&&(flags&4)){
        for(Module *m=modules;m;m=m->next)if(m->base==base&&m->size&&visible(m)&&pointer>=m->image&&(uint64_t)pointer<(uint64_t)m->image+m->size){found=m;break;}
    }else found=find_name(base,pointer?name:NULL,full);
    if(!found)error=126;
    else if(!(flags&2)&&!found->pinned&&!(flags&1)&&found->references==UINT32_MAX)error=8;
    else {
        if(flags&1)found->pinned=1;
        else if(!(flags&2)&&!found->pinned)++found->references;
        value=found->image;
    }
    memcpy((void *)(uintptr_t)(base+output),&value,4);pthread_mutex_unlock(&lock);
    if(error)tribes_set_last_error(error);return !error;
failed:
    memcpy((void *)(uintptr_t)(base+output),&value,4);tribes_set_last_error(error);return 0;
}
uint32_t tribes_get_module_handle_ex_w(uint64_t base,uint32_t flags,uint32_t name,uint32_t output) {return module_handle_ex(base,flags,name,output,1);}
uint32_t tribes_get_module_handle_ex_a(uint64_t base,uint32_t flags,uint32_t name,uint32_t output) {return module_handle_ex(base,flags,name,output,0);}

uint32_t tribes_get_module_filename(uint64_t base,uint32_t image,uint32_t output,uint32_t capacity) {
    if(!capacity){tribes_set_last_error(122);return 0;}
    if(!output||output>UINT32_MAX-(capacity-1)||base>UINTPTR_MAX-output-(capacity-1)){tribes_set_last_error(87);return 0;}
    pthread_mutex_lock(&lock);Module *m=modules;while(m&&(m->base!=base||(image?m->image!=image:!m->main)))m=m->next;
    if(!m||!visible(m)){pthread_mutex_unlock(&lock);tribes_set_last_error(126);return 0;}
    size_t n=strlen(m->path);char *buffer=(char *)(uintptr_t)(base+output);
    /* Modern Windows truncation: NUL-terminated output and error 122, return nSize. */
    if(n>=capacity){memcpy(buffer,m->path,capacity-1);buffer[capacity-1]=0;pthread_mutex_unlock(&lock);tribes_set_last_error(122);return capacity;}
    memcpy(buffer,m->path,n+1);pthread_mutex_unlock(&lock);return (uint32_t)n;
}

/* Wide view of the declared ASCII guest module namespace. Modern Windows
   truncation matches the existing ANSI helper, despite the partial XP version
   identity. Non-ASCII registry paths require an explicit encoding contract. */
uint32_t tribes_get_module_filename_w(uint64_t base,uint32_t image,uint32_t output,uint32_t capacity) {
    if(!capacity){tribes_set_last_error(122);return 0;}
    uint64_t bytes=2ull*capacity;
    if(!output||bytes>UINT64_C(0x100000000)-output||base>UINTPTR_MAX-output-(bytes-1)){tribes_set_last_error(87);return 0;}
    pthread_mutex_lock(&lock);Module *m=modules;while(m&&(m->base!=base||(image?m->image!=image:!m->main)))m=m->next;
    if(!m||!visible(m)){pthread_mutex_unlock(&lock);tribes_set_last_error(126);return 0;}
    size_t n=strlen(m->path);
    for(size_t i=0;i<n;i++)if((unsigned char)m->path[i]>127){fprintf(stderr,"TribesPad: unsupported non-ASCII module filename metadata\n");abort();}
    size_t copied=n<capacity?n:capacity-1;
    for(size_t i=0;i<copied;i++){uint16_t unit=(unsigned char)m->path[i];memcpy((void *)(uintptr_t)(base+output+2*i),&unit,2);}
    uint16_t zero=0;memcpy((void *)(uintptr_t)(base+output+2*copied),&zero,2);pthread_mutex_unlock(&lock);
    if(n>=capacity){tribes_set_last_error(122);return capacity;}return (uint32_t)n;
}

int tribes_module_define_native(uint64_t base,uint32_t image,const char *path,const TribesNativeExport *symbols,uint32_t count) {
    if(!symbols||!count||count>4096){errno=EINVAL;return 0;}
    Export *copy=calloc(count,sizeof *copy);if(!copy)return 0;
    for(uint32_t i=0;i<count;i++) {
        if(!symbols[i].name||!symbols[i].name[0]||strnlen(symbols[i].name,256)==256||!symbols[i].address){free(copy);errno=EINVAL;return 0;}
        for(uint32_t j=0;j<i;j++)if(!strcmp(copy[j].name,symbols[i].name)||(symbols[i].ordinal&&copy[j].ordinal==symbols[i].ordinal)){free(copy);errno=EINVAL;return 0;}
        strcpy(copy[i].name,symbols[i].name);copy[i].ordinal=symbols[i].ordinal;copy[i].address=symbols[i].address;
    }
    /* Publish definition atomically: a provider cannot appear loaded between metadata
       registration and export publication. */
    if(!image||!path||strnlen(path,4096)==4096||strlen(path)<4||path[1]!=':'||path[2]!='\\'){free(copy);errno=EINVAL;return 0;}
    Module *entry=calloc(1,sizeof *entry);if(!entry){free(copy);return 0;}
    entry->base=base;entry->image=image;entry->native=1;entry->exports=copy;entry->count=count;strcpy(entry->path,path);
    pthread_mutex_lock(&lock);if(absent_leaf(base,path)){pthread_mutex_unlock(&lock);free(copy);free(entry);errno=EALREADY;return 0;}for(Module *m=modules;m;m=m->next)if(m->base==base&&(overlaps(image,0,m->image,m->size)||!strcasecmp(m->path,path))){pthread_mutex_unlock(&lock);free(copy);free(entry);errno=EALREADY;return 0;}
    entry->next=modules;modules=entry;pthread_mutex_unlock(&lock);return 1;
}
/* Loader-owned references for providers present at guest process startup.
   Ordinary LoadLibrary/FreeLibrary references remain independently balanced.
   https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary */
int tribes_module_retain_native(uint64_t base,uint32_t image){
    int found=0,ok=0;pthread_mutex_lock(&lock);
    for(Module *m=modules;m;m=m->next)if(m->base==base&&m->image==image&&m->native){found=1;if(m->references<UINT32_MAX){m->references++;ok=1;}break;}
    pthread_mutex_unlock(&lock);if(!ok)errno=found?EOVERFLOW:EINVAL;return ok;
}
static uint32_t load_name(uint64_t base,const char *guest) {
    char name[4096];int full=0;
    if(!normalize_name(guest,name,&full))return 0;
    const char *leaf;
    uint32_t result=0;pthread_mutex_lock(&lock);
    for(Module *m=modules;m;m=m->next)if(m->base==base){leaf=strrchr(m->path,'\\');leaf=leaf?leaf+1:m->path;if(!strcasecmp(full?m->path:leaf,name)){if(m->pending){pthread_mutex_unlock(&lock);fprintf(stderr,"TribesPad: pending AOT module requires CPU-aware load\n");abort();}if(m->pinned||m->references<UINT32_MAX){if(!m->pinned)m->references++;result=m->image;}break;}}
    int absent=!full&&absent_leaf(base,name);pthread_mutex_unlock(&lock);
    if(!result&&absent){tribes_set_last_error(126);return 0;}
    if(!result){fprintf(stderr,"TribesPad: native library provider unavailable for %s\n",name);abort();}
    return result;
}
uint32_t tribes_load_library(uint64_t base,uint32_t pointer) {
    if(!pointer||pointer>UINT32_MAX-4095||base>UINTPTR_MAX-pointer-4095){tribes_set_last_error(87);return 0;}
    return load_name(base,(const char *)(uintptr_t)(base+pointer));
}
/* Legacy XP API profile: SEARCH_SYSTEM32 is unavailable and fails with 87.
   The selected QoL CRT explicitly handles that result before its unflagged
   kernel32 fallback. This is not a System32 search on the Apple filesystem.
   Flags zero loads only already-defined/mapped providers, as LoadLibraryA.
   Wide names currently cover the ASCII namespace; other Unicode is explicit.
   https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw */
uint32_t tribes_load_library_ex_a(uint64_t base,uint32_t pointer,uint32_t file,uint32_t flags) {
    if(!pointer||file){tribes_set_last_error(87);return 0;}
    if(flags==0x800){tribes_set_last_error(87);return 0;}
    if(flags){fprintf(stderr,"TribesPad: unsupported LoadLibraryExA flags 0x%x\n",flags);abort();}
    return tribes_load_library(base,pointer);
}
uint32_t tribes_load_library_ex_w(uint64_t base,uint32_t pointer,uint32_t file,uint32_t flags) {
    if(!pointer||file){tribes_set_last_error(87);return 0;}
    if(flags==0x800){tribes_set_last_error(87);return 0;}
    if(flags){fprintf(stderr,"TribesPad: unsupported LoadLibraryExW flags 0x%x\n",flags);abort();}
    char name[4096];
    for(uint32_t i=0;i<sizeof name;i++) {
        uint64_t offset=(uint64_t)pointer+2*i;
        if(offset>UINT32_MAX-1||base>UINTPTR_MAX-offset-1){tribes_set_last_error(87);return 0;}
        uint16_t unit;memcpy(&unit,(void *)(uintptr_t)(base+offset),2);
        if(unit>127){fprintf(stderr,"TribesPad: unsupported non-ASCII LoadLibraryExW name\n");abort();}
        name[i]=(char)unit;
        if(!unit)return load_name(base,name);
    }
    tribes_set_last_error(126);return 0;
}
uint32_t tribes_free_library(uint64_t base,uint32_t image) {
    uint32_t result=0;pthread_mutex_lock(&lock);for(Module *m=modules;m;m=m->next)if(m->base==base&&m->image==image){if(m->pinned)result=1;
        else if(m->references>(m->native?0u:1u)){m->references--;result=1;}
        else if(!m->native){fprintf(stderr,"TribesPad: mapped image unload requires original detach/unmap at 0x%08x\n",image);abort();}break;}
    pthread_mutex_unlock(&lock);if(!result)tribes_set_last_error(6);return result;
}
uint32_t tribes_get_proc_address(uint64_t base,uint32_t image,uint32_t pointer) {
    const char *name=NULL;uint16_t ordinal=0;
    if(pointer<=0xffff)ordinal=(uint16_t)pointer;
    else {if(pointer>UINT32_MAX-255||base>UINTPTR_MAX-pointer-255){tribes_set_last_error(87);return 0;}name=(const char *)(uintptr_t)(base+pointer);if(strnlen(name,256)==256){tribes_set_last_error(127);return 0;}}
    uint32_t result=0;int valid=0;pthread_mutex_lock(&lock);
    for(Module *m=modules;m;m=m->next)if(m->base==base&&m->image==image&&m->native&&visible(m)){valid=1;for(uint32_t i=0;i<m->count;i++)if(name?!strcmp(name,m->exports[i].name):(ordinal&&ordinal==m->exports[i].ordinal)){result=m->exports[i].address;break;}break;}
    pthread_mutex_unlock(&lock);if(!result)tribes_set_last_error(valid?127:6);return result;
}
