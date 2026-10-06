/* Win32 ANSI environment snapshots in original guest-address memory.
   Contracts: https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentstrings
   https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-freeenvironmentstringsa */
#include "process_environment.h"
#include "guest_heap.h"
#include "guest_filesystem.h"
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef struct Snapshot { uint32_t address,bytes;int wide;struct Snapshot *next; } Snapshot;
typedef struct Environment {
    uint64_t base;char *windows_directory;unsigned char *wide_command_line;uint32_t wide_command_length,wide_command_address;unsigned char *command_line;uint32_t command_length,command_address;unsigned char *block;uint32_t length;Snapshot *snapshots;struct Environment *next;
} Environment;
#include "cp437_environment_table.inc"
static Environment *environments;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Environment *find(uint64_t base) {
    for(Environment *env=environments;env;env=env->next)if(env->base==base)return env;
    return NULL;
}
static int valid(const unsigned char *block,uint32_t length) {
    if(!block||length<2||length>1048576||block[length-1]||block[length-2])return 0;
    if(length==2)return !block[0];
    uint32_t offset=0;
    while(offset<length-1) {
        const unsigned char *end=memchr(block+offset,0,length-1-offset);
        if(!end||end==block+offset)return 0;
        const unsigned char *equals=memchr(block+offset,'=',(size_t)(end-block-offset));
        if(!equals||equals==block+offset)return 0;
        offset=(uint32_t)(end-block)+1;
    }
    return offset==length-1;
}
uint32_t tribes_environment_configure(uint64_t base,const void *data,uint32_t length) {
    if(!valid(data,length)){tribes_set_last_error(87);return 0;}
    unsigned char *block=malloc(length);if(!block){tribes_set_last_error(8);return 0;}
    memcpy(block,data,length);pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env) {
        env=calloc(1,sizeof(*env));if(!env){pthread_mutex_unlock(&lock);free(block);tribes_set_last_error(8);return 0;}
        env->base=base;env->next=environments;environments=env;
    }
    free(env->block);env->block=block;env->length=length;
    pthread_mutex_unlock(&lock);return 1;
}
static uint32_t get_environment_snapshot(uint64_t base,int wide) {
    pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env||!env->block) { fprintf(stderr,"TribesPad: guest environment was not configured\n");abort(); }
    Snapshot *snapshot=malloc(sizeof(*snapshot));
    if(!snapshot){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
    uint32_t bytes=env->length*(wide?2u:1u);
    uint32_t address=tribes_global_alloc(base,0,bytes);
    if(!address){free(snapshot);pthread_mutex_unlock(&lock);return 0;}
    if(wide)for(uint32_t i=0;i<env->length;i++){uint16_t unit=cp437_environment_to_unicode[env->block[i]];memcpy((void *)(uintptr_t)(base+address+2ull*i),&unit,2);}
    else memcpy((void *)(uintptr_t)(base+address),env->block,env->length);
    snapshot->address=address;snapshot->bytes=bytes;snapshot->wide=wide;snapshot->next=env->snapshots;env->snapshots=snapshot;
    pthread_mutex_unlock(&lock);return address;
}
static uint32_t free_environment_snapshot(uint64_t base,uint32_t address,int wide) {
    pthread_mutex_lock(&lock);Environment *env=find(base);Snapshot **link=env?&env->snapshots:NULL;
    while(link&&*link&&((*link)->address!=address||(*link)->wide!=wide))link=&(*link)->next;
    if(!link||!*link){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
    if(tribes_global_free(base,address)){pthread_mutex_unlock(&lock);return 0;}
    Snapshot *snapshot=*link;*link=snapshot->next;free(snapshot);pthread_mutex_unlock(&lock);return 1;
}

uint32_t tribes_get_environment_strings(uint64_t base){return get_environment_snapshot(base,0);}
uint32_t tribes_get_environment_strings_w(uint64_t base){return get_environment_snapshot(base,1);}
uint32_t tribes_free_environment_strings(uint64_t base,uint32_t pointer){return free_environment_snapshot(base,pointer,0);}
uint32_t tribes_free_environment_strings_w(uint64_t base,uint32_t pointer){return free_environment_snapshot(base,pointer,1);}
uint32_t tribes_environment_snapshot_size(uint64_t base,uint32_t pointer,int wide) {
    uint32_t bytes=0;pthread_mutex_lock(&lock);Environment *env=find(base);
    for(Snapshot *p=env?env->snapshots:NULL;p;p=p->next)if(p->address==pointer&&p->wide==!!wide){bytes=p->bytes;break;}
    pthread_mutex_unlock(&lock);return bytes;
}

uint32_t tribes_command_line_configure(uint64_t base,const void *data,uint32_t length) {
    if(!data||!length||length>65536||((const unsigned char *)data)[length-1]||memchr(data,0,length-1)) { tribes_set_last_error(87);return 0; }
    unsigned char *text=malloc(length);if(!text){tribes_set_last_error(8);return 0;}memcpy(text,data,length);
    pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env) { env=calloc(1,sizeof(*env));if(!env){pthread_mutex_unlock(&lock);free(text);tribes_set_last_error(8);return 0;}env->base=base;env->next=environments;environments=env; }
    if(env->command_address){pthread_mutex_unlock(&lock);free(text);tribes_set_last_error(87);return 0;}
    free(env->command_line);env->command_line=text;env->command_length=length;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_get_command_line(uint64_t base) {
    pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env||!env->command_line) { fprintf(stderr,"TribesPad: guest command line was not configured\n");abort(); }
    if(!env->command_address) {
        uint32_t address=tribes_global_alloc(base,0,env->command_length);
        if(!address){pthread_mutex_unlock(&lock);return 0;}
        memcpy((void *)(uintptr_t)(base+address),env->command_line,env->command_length);env->command_address=address;
    }
    uint32_t result=env->command_address;pthread_mutex_unlock(&lock);return result;
}

/* UTF-16LE code units are retained byte-for-byte, with process-owned lifetime.
   https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getcommandlinew */
uint32_t tribes_command_line_configure_w(uint64_t base,const void *data,uint32_t length) {
    const unsigned char *bytes=data;
    if(!bytes||length<2||length>65536||(length&1)||bytes[length-1]||bytes[length-2]) { tribes_set_last_error(87);return 0; }
    for(uint32_t i=0;i<length-2;i+=2)if(!bytes[i]&&!bytes[i+1]) { tribes_set_last_error(87);return 0; }
    unsigned char *text=malloc(length);if(!text){tribes_set_last_error(8);return 0;}memcpy(text,data,length);
    pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env) { env=calloc(1,sizeof(*env));if(!env){pthread_mutex_unlock(&lock);free(text);tribes_set_last_error(8);return 0;}env->base=base;env->next=environments;environments=env; }
    if(env->wide_command_address){pthread_mutex_unlock(&lock);free(text);tribes_set_last_error(87);return 0;}
    free(env->wide_command_line);env->wide_command_line=text;env->wide_command_length=length;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_get_command_line_w(uint64_t base) {
    pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env||!env->wide_command_line) { fprintf(stderr,"TribesPad: guest command line was not configured\n");abort(); }
    if(!env->wide_command_address) {
        uint32_t address=tribes_global_alloc(base,0,env->wide_command_length);
        if(!address){pthread_mutex_unlock(&lock);return 0;}
        memcpy((void *)(uintptr_t)(base+address),env->wide_command_line,env->wide_command_length);env->wide_command_address=address;
    }
    uint32_t result=env->wide_command_address;pthread_mutex_unlock(&lock);return result;
}

/* Absolute canonical ASCII drive paths only. Relative/UNC/device paths,
   Unicode/OEM values and normalization modes need a real path service. */
static int temp_path_supported(const char *path) {
    size_t n=strlen(path);
    if(n<3||!((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))||path[1]!=':'||path[2]!='\\')return 0;
    const char *component=path+3;
    for(const char *p=component;;p++) {
        unsigned char ch=(unsigned char)*p;
        if(ch>=128||ch=='/'||ch==':'||ch=='*'||ch=='?'||ch=='"'||ch=='<'||ch=='>'||ch=='|')return 0;
        if(!ch||ch=='\\') {
            size_t size=(size_t)(p-component);
            if((size==1&&component[0]=='.')||(size==2&&component[0]=='.'&&component[1]=='.')||(!size&&ch))return 0;
            if(!ch)return 1;
            component=p+1;
        }
    }
}
uint32_t tribes_temp_directory_configure(uint64_t base,const char *directory) {
    if(!directory||strnlen(directory,261)>260||!temp_path_supported(directory)){tribes_set_last_error(87);return 0;}
    char *copy=strdup(directory);if(!copy){tribes_set_last_error(8);return 0;}
    pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env){env=calloc(1,sizeof *env);if(!env){pthread_mutex_unlock(&lock);free(copy);tribes_set_last_error(8);return 0;}env->base=base;env->next=environments;environments=env;}
    free(env->windows_directory);env->windows_directory=copy;pthread_mutex_unlock(&lock);return 1;
}
static const char *environment_value(const Environment *env,const char *name) {
    size_t n=strlen(name);
    for(uint32_t offset=0;offset<env->length-1;) {
        const char *entry=(const char *)env->block+offset;size_t length=strlen(entry);
        if(length>n&&entry[n]=='='&&!strncasecmp(entry,name,n)&&entry[n+1])return entry+n+1;
        offset+=(uint32_t)length+1;
    }
    return NULL;
}
/* Paths in the existing 32-bit guest namespace, not directories on macOS.
   Size queries include NUL; successful copies return the length without NUL.
   Short-buffer contents are unspecified by Win32; this profile leaves them
   untouched. Neither query reads mutable TMP/TEMP environment variables.
   https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getwindowsdirectorya
   https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemdirectorya */
static uint32_t installation_directory_a(uint64_t base,uint32_t output,uint32_t capacity,int system){
    char path[272];pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env||!env->windows_directory){pthread_mutex_unlock(&lock);tribes_set_last_error(3);return 0;}
    size_t n=strlen(env->windows_directory);memcpy(path,env->windows_directory,n+1);pthread_mutex_unlock(&lock);
    while(n>3&&path[n-1]=='\\')path[--n]=0;
    if(system){if(path[n-1]!='\\')path[n++]='\\';memcpy(path+n,"System32",9);n+=8;}
    if(capacity<=n)return (uint32_t)n+1;
    uint64_t end=(uint64_t)output+n+1;
    if(!output||end>UINT64_C(0x100000000)||base>UINTPTR_MAX-end){tribes_set_last_error(87);return 0;}
    memcpy((void *)(uintptr_t)(base+output),path,n+1);return (uint32_t)n;
}
uint32_t tribes_get_windows_directory_a(uint64_t base,uint32_t output,uint32_t capacity){return installation_directory_a(base,output,capacity,0);}
uint32_t tribes_get_system_directory_a(uint64_t base,uint32_t output,uint32_t capacity){return installation_directory_a(base,output,capacity,1);}
/* The pinned font loader asks only for CSIDL_FONTS, the current process,
   and the current path. Report a real directory in the configured C: mount;
   otherwise return its actual failure and leave the shell output empty.
   https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetfolderpatha */
uint32_t tribes_sh_get_folder_path_a(uint64_t base,uint32_t window,uint32_t folder,uint32_t token,uint32_t flags,uint32_t output){
    if(window||folder!=0x14||token||flags){fprintf(stderr,"TribesPad: unsupported shell folder query\n");abort();}
    uint32_t error=tribes_get_last_error();
    if(!output||output>UINT32_MAX-259||base>UINTPTR_MAX-output-260)return UINT32_C(0x80070057);
    char *destination=(void *)(uintptr_t)(base+output);destination[0]=0;
    char logical[261];pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env||!env->windows_directory){pthread_mutex_unlock(&lock);return UINT32_C(0x80004005);}
    size_t n=strlen(env->windows_directory);memcpy(logical,env->windows_directory,n+1);pthread_mutex_unlock(&lock);
    while(n>3&&logical[n-1]=='\\')logical[--n]=0;
    if(n+(logical[n-1]=='\\'?0:1)+5>=260)return UINT32_C(0x800700ce);
    if(logical[n-1]!='\\')logical[n++]='\\';memcpy(logical+n,"Fonts",6);
    char host[PATH_MAX];struct stat st;uint32_t result;
    if(!tribes_filesystem_resolve(base,logical,host,sizeof host)||stat(host,&st)){
        int e=errno;result=e==ENOENT||e==ENOTDIR?UINT32_C(0x80070003):e==EACCES?UINT32_C(0x80070005):UINT32_C(0x80004005);
    }else if(!S_ISDIR(st.st_mode))result=UINT32_C(0x80070003);
    else{memcpy(destination,logical,n+6);result=0;}
    tribes_set_last_error(error);return result;
}
/* GetTempPathW and GetTempPath2W share ordinary non-SYSTEM process behavior:
   TMP, TEMP, USERPROFILE, then the configured Windows directory. This runtime
   does not impersonate SYSTEM. Directory existence/permissions are not queried.
   UTF-16 output includes a trailing backslash; short buffers return required
   units including NUL. Their unspecified contents are retained in this profile.
   https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettemppath2w */
uint32_t tribes_get_temp_path_w(uint64_t base,uint32_t capacity,uint32_t output) {
    char path[261];pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env||!env->block||!env->windows_directory){fprintf(stderr,"TribesPad: temporary directory environment/profile not configured\n");abort();}
    const char *value=environment_value(env,"TMP");
    if(!value)value=environment_value(env,"TEMP");
    if(!value)value=environment_value(env,"USERPROFILE");
    if(!value)value=env->windows_directory;
    size_t n=strlen(value);
    if(n>260){pthread_mutex_unlock(&lock);tribes_set_last_error(206);return 0;}
    memcpy(path,value,n+1);pthread_mutex_unlock(&lock);
    if(!temp_path_supported(path)){fprintf(stderr,"TribesPad: unsupported temporary path namespace/normalization\n");abort();}
    if(path[n-1]!='\\'){if(n==260){tribes_set_last_error(206);return 0;}path[n++]='\\';path[n]=0;}
    if(capacity<=n)return (uint32_t)n+1;
    uint64_t bytes=2ull*(n+1),end=(uint64_t)output+bytes;
    if(!output||end>UINT64_C(0x100000000)||base>UINTPTR_MAX-end){tribes_set_last_error(87);return 0;}
    for(size_t i=0;i<=n;i++){uint16_t ch=(unsigned char)path[i];memcpy((void *)(uintptr_t)(base+output+2*i),&ch,2);}
    return (uint32_t)n;
}

/* ASCII names and values from the configured guest block. This never imports
   the host process's environment. OEM/ANSI conversion is explicit work still
   required before non-ASCII names/values can be returned by this ANSI API.
   https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentvariablea */
uint32_t tribes_get_environment_variable_a(uint64_t base,uint32_t pointer,uint32_t output,uint32_t capacity) {
    if(!pointer){tribes_set_last_error(203);return 0;}
    char name[32767];size_t length;
    for(length=0;length<sizeof name;length++) {
        uint64_t address=(uint64_t)pointer+length;
        if(address>UINT32_MAX||base>UINTPTR_MAX-address){tribes_set_last_error(87);return 0;}
        unsigned char ch=*(const unsigned char *)(uintptr_t)(base+address);
        if(ch>=128){fprintf(stderr,"TribesPad: unsupported non-ASCII environment name\n");abort();}
        name[length]=(char)ch;if(!ch)break;
    }
    if(length==sizeof name){fprintf(stderr,"TribesPad: unsupported oversized environment name\n");abort();}
    if(!length){tribes_set_last_error(203);return 0;}
    pthread_mutex_lock(&lock);Environment *env=find(base);
    if(!env||!env->block){fprintf(stderr,"TribesPad: environment lookup profile not configured\n");abort();}
    const char *value=NULL;
    for(uint32_t offset=0;offset<env->length-1;) {
        const char *entry=(const char *)env->block+offset;size_t size=strlen(entry);
        if(size>length&&entry[length]=='='&&!strncasecmp(entry,name,length)){value=entry+length+1;break;}
        offset+=(uint32_t)size+1;
    }
    if(!value){pthread_mutex_unlock(&lock);tribes_set_last_error(203);return 0;}
    size_t n=strlen(value);
    if(n>=32767){fprintf(stderr,"TribesPad: unsupported oversized environment value\n");abort();}
    for(size_t i=0;i<n;i++)if((unsigned char)value[i]>=128){fprintf(stderr,"TribesPad: unsupported OEM/ANSI environment value conversion\n");abort();}
    if(capacity<=n){pthread_mutex_unlock(&lock);return (uint32_t)n+1;}
    uint64_t end=(uint64_t)output+n+1;
    if(!output||end>UINT64_C(0x100000000)||base>UINTPTR_MAX-end){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
    memcpy((void *)(uintptr_t)(base+output),value,n+1);pthread_mutex_unlock(&lock);return (uint32_t)n;
}
