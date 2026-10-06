#include "guest_file.h"
#include "guest_filesystem.h"
#include "guest_heap.h"
#include "native_handles.h"
#include "guest_memory_checks.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/attr.h>
#include <sys/mount.h>
#include <unistd.h>
/* x86 records are packed explicitly; native structs never cross the guest ABI. */
static void nt_u32(unsigned char *p,size_t offset,uint32_t value){memcpy(p+offset,&value,4);}
static void nt_u64(unsigned char *p,size_t offset,uint64_t value){memcpy(p+offset,&value,8);}
static int nt_time(unsigned char *p,size_t offset,struct timespec time){
    if(time.tv_sec<INT64_C(-11644473600)||time.tv_nsec<0||time.tv_nsec>=1000000000)return 0;
    uint64_t seconds=time.tv_sec>=0?(uint64_t)time.tv_sec+UINT64_C(11644473600):(uint64_t)(time.tv_sec+INT64_C(11644473600));
    if(seconds>(INT64_MAX-(uint64_t)time.tv_nsec/100)/10000000)return 0;
    nt_u64(p,offset,seconds*10000000+(uint64_t)time.tv_nsec/100);return 1;
}
static uint32_t nt_output_check(uint64_t base,uint32_t iosb,uint32_t output,uint32_t length){
    if(!base||!iosb||!output||iosb>UINT32_MAX-7||output>UINT32_MAX-(length-1)||
       base>UINTPTR_MAX-iosb-7||base>UINTPTR_MAX-output-(length-1))return 0xc0000005u;
    if((iosb|output)&3)return 0x80000002u;
    return 0;
}
static uint32_t nt_complete(uint64_t base,uint32_t iosb,uint32_t output,const void *record,uint32_t length,uint32_t status){
    uint32_t error=nt_output_check(base,iosb,output,length);if(error)return error;
    uint32_t completion[2]={status,length};
    void *destination=(void *)(uintptr_t)(base+output),*io=(void *)(uintptr_t)(base+iosb);
    /* Preflight both ranges before either copy, and retain the permission epoch. */
    uint32_t a=tribes_memory_access_begin(destination,length,2);
    uint32_t b=tribes_memory_access_begin(io,8,2);
    memcpy(destination,record,length);memcpy(io,completion,8);
    tribes_memory_access_end(b);tribes_memory_access_end(a);return status;
}
uint32_t tribes_nt_query_volume_information_file(uint64_t base,uint32_t handle,
    uint32_t iosb,uint32_t output,uint32_t capacity,uint32_t information_class) {
    if(!information_class)return 0xc0000003u;
    if(information_class!=4&&information_class!=1){fprintf(stderr,"TribesPad: unsupported NtQueryVolumeInformationFile class %u\n",information_class);abort();}
    uint32_t minimum=information_class==4?8:24;
    if(capacity<minimum)return 0xc0000004u;
    uint32_t status=nt_output_check(base,iosb,output,minimum);if(status)return status;
    int fd=tribes_fd_borrow_nt(handle,&status);if(fd<0)return status;
    struct stat st;struct statfs fs;
    if(fstat(fd,&st)){close(fd);return 0xc0000001u;}
    if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)&&!S_ISLNK(st.st_mode)){
        close(fd);fputs("TribesPad: unsupported NtQueryVolumeInformationFile device object\n",stderr);abort();
    }
    if(fstatfs(fd,&fs)){close(fd);return 0xc0000001u;}
    close(fd);
    int local=(fs.f_flags&MNT_LOCAL)!=0;
    if(local?strcmp(fs.f_fstypename,"apfs")&&strcmp(fs.f_fstypename,"hfs"):
             strcmp(fs.f_fstypename,"nfs")&&strcmp(fs.f_fstypename,"smbfs")&&strcmp(fs.f_fstypename,"afpfs")){
        fprintf(stderr,"TribesPad: unsupported NT volume filesystem %s\n",fs.f_fstypename);abort();
    }
    if(information_class==4){
        uint32_t device[2]={local?8u:0x14u,(fs.f_flags&MNT_RDONLY?2u:0u)|(local?0u:0x10u)};
        return nt_complete(base,iosb,output,device,8,0);
    }
    /* C: is the explicit TribesPad virtual volume, backed by this native mount.
       Its serial is the native mount ID (session identity, not a physical disk
       serial); it has no Windows object IDs or recorded volume creation time.
       Never expose host volume labels/paths as the guest namespace. */
    static const char label[]="TribesPad";
    unsigned char record[18+2*(sizeof label-1)]={0};
    nt_u32(record,8,(uint32_t)fs.f_fsid.val[0]);nt_u32(record,12,2*(sizeof label-1));
    uint32_t length=capacity<sizeof record?capacity&~1u:sizeof record;
    for(uint32_t i=18;i<length;i+=2)record[i]=(unsigned char)label[(i-18)/2];
    return nt_complete(base,iosb,output,record,length,length<sizeof record?0x80000005u:0);
}
uint32_t tribes_nt_query_information_file(uint64_t base,uint32_t handle,
    uint32_t iosb,uint32_t output,uint32_t capacity,uint32_t information_class){
    if(!information_class)return 0xc0000003u;
    if(information_class!=18){fprintf(stderr,"TribesPad: unsupported NtQueryInformationFile class %u\n",information_class);abort();}
    if(capacity<104)return 0xc0000004u;
    uint32_t status=nt_output_check(base,iosb,output,104);if(status)return status;
    TribesFileOpenInfo info;int fd=tribes_named_file_borrow_nt(handle,&status,&info);if(fd<0)return status;
    struct stat st,named;off_t position;
    if(fstat(fd,&st)){close(fd);return 0xc0000001u;}
    if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)){
        close(fd);fputs("TribesPad: unsupported NtQueryInformationFile object\n",stderr);abort();
    }
    /* Guest rename/delete APIs are not implemented. Detect a changed host name
       instead of returning a stale path for a different object. */
    if(lstat(info.host,&named)||st.st_dev!=named.st_dev||st.st_ino!=named.st_ino){close(fd);return 0xc00000bbu;}
    position=lseek(fd,0,SEEK_CUR);close(fd);
    if(position<0)return 0xc0000001u;
    if(st.st_size<0||st.st_blocks<0||(uint64_t)st.st_blocks>INT64_MAX/512)return 0xc00000bbu;
    unsigned char record[100+2*PATH_MAX]={0};
    if(!nt_time(record,0,st.st_birthtimespec)||!nt_time(record,8,st.st_atimespec)||
       !nt_time(record,16,st.st_mtimespec)||!nt_time(record,24,st.st_ctimespec))return 0xc00000bbu;
    uint32_t attributes=S_ISDIR(st.st_mode)?16:32;
    if(!(st.st_mode&S_IWUSR))attributes|=1;
    if(st.st_flags&UF_HIDDEN)attributes|=2;
    nt_u32(record,32,attributes);
    nt_u64(record,40,S_ISDIR(st.st_mode)?0:(uint64_t)st.st_blocks*512);
    nt_u64(record,48,S_ISDIR(st.st_mode)?0:(uint64_t)st.st_size);
    nt_u32(record,56,st.st_nlink);record[61]=S_ISDIR(st.st_mode)!=0;
    nt_u64(record,64,st.st_ino);nt_u32(record,76,info.access);
    nt_u64(record,80,(uint64_t)position);nt_u32(record,88,info.mode);
    /* No Windows EAs, delete-on-close, or unbuffered IO in this filesystem
       profile. EA size, DeletePending and buffered alignment are zero. */
    uint32_t name_bytes=(uint32_t)strlen(info.name)*2;nt_u32(record,96,name_bytes);
    uint32_t copied=capacity-100;if(copied>name_bytes)copied=name_bytes;copied&=~1u;
    for(uint32_t i=0;i<copied;i+=2)record[100+i]=(unsigned char)info.name[i/2];
    return nt_complete(base,iosb,output,record,100+copied,copied<name_bytes?0x80000005u:0);
}
/* Shared checked ASCII A/W decoding for existing-path file services. */
static int read_path(uint64_t base,uint32_t name,int wide,char guest[PATH_MAX]) {
    if(!name){tribes_set_last_error(87);return 0;}
    /* The mounted namespace currently supports ASCII filenames. Decode each
       UTF16 code unit without truncation; other characters retain the same
       explicit ERROR_NOT_SUPPORTED as the ANSI namespace resolver. */
    size_t length=0;
    for(;length<PATH_MAX;length++){
        size_t offset=length*(wide?2u:1u),last=offset+(wide?1u:0u);
        if(name>UINT32_MAX-last||base>UINTPTR_MAX-name-last){tribes_set_last_error(87);return 0;}
        uint16_t value;
        if(wide)memcpy(&value,(const void *)(uintptr_t)(base+name+offset),2);
        else value=*(const unsigned char *)(uintptr_t)(base+name+offset);
        if(value>127){tribes_set_last_error(50);return 0;}
        guest[length]=(char)value;if(!value)break;
    }
    if(length==PATH_MAX){tribes_set_last_error(206);return 0;}if(!length){tribes_set_last_error(3);return 0;}
    return 1;
}
/* Default security inherits the real host directory policy. Explicit Windows
   security descriptors remain unsupported, rather than silently discarded. */
static uint32_t create_directory(uint64_t base,uint32_t name,uint32_t security,int wide) {
    if(security){fputs("TribesPad: unsupported CreateDirectory security attributes\n",stderr);abort();}
    char guest[PATH_MAX];if(!read_path(base,name,wide,guest))return 0;
    uint32_t saved=tribes_get_last_error();
    if(tribes_filesystem_create_directory(base,guest)){tribes_set_last_error(saved);return 1;}
    int e=errno;
    tribes_set_last_error(e==EEXIST?183:e==ENOENT||e==ENOTDIR||e==ENODEV?3:
        e==ENOSPC?112:e==ENOTSUP?50:e==EINVAL?123:e==ENAMETOOLONG?206:5);return 0;
}
uint32_t tribes_create_directory(uint64_t base,uint32_t name,uint32_t security){return create_directory(base,name,security,0);}
uint32_t tribes_create_directory_w(uint64_t base,uint32_t name,uint32_t security){return create_directory(base,name,security,1);}
static uint32_t path_error(uint64_t base,const char *guest,int native_error) {
        uint32_t error=native_error==ENOENT?2:native_error==ENOTDIR||native_error==ENODEV?3:native_error==ENOTSUP?50:native_error==EINVAL?123:native_error==ENAMETOOLONG?206:native_error==ELOOP?1921:5;
        if(error==2){char parent[PATH_MAX];strcpy(parent,guest);char *a=strrchr(parent,'/'),*b=strrchr(parent,'\\'),*last=!a?b:!b?a:(a>b?a:b);if(last){if(last==parent||(last==parent+2&&parent[1]==':'))last[1]=0;else *last=0;}else if(parent[1]==':')parent[2]=0;else strcpy(parent,".");char resolved[PATH_MAX];if(!tribes_filesystem_resolve(base,parent,resolved,sizeof resolved))error=3;}
        return error;
}
/* Existing files and explicit backup-directory/reparse opens in the configured
   guest namespace. No Windows backup-privilege elevation or token/DACL model.
   Host descriptors retain native authority; later mutations must enforce it.
   CREATE_ALWAYS/OPEN_ALWAYS support writable regular files with default security. Other creation
   dispositions, overlapped/device flags and templates remain gates. */
static uint32_t create_file(uint64_t base,uint32_t name,uint32_t access,uint32_t share,uint32_t security,uint32_t creation,uint32_t flags,uint32_t template_handle,int wide){
    if((creation!=3&&creation!=2&&creation!=4)||template_handle||(flags&~UINT32_C(0x0a200080))||(access&~UINT32_C(0xc0010180))){fprintf(stderr,"TribesPad: unsupported CreateFile access/creation/flags/template\n");abort();}
    if(share&~7u||!name){tribes_set_last_error(87);return UINT32_MAX;}
    /* SECURITY_ATTRIBUTES has three 32-bit fields in the guest ABI. For
       OPEN_EXISTING the descriptor pointer is ignored, even when non-NULL;
       inheritance still applies to the returned handle. Never dereference the
       ignored descriptor or pass a guest structure to the native open call.
       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
       Child-process creation itself remains outside this runtime's API set. */
    uint32_t inherit=0;
    if(security){
        if(security>UINT32_MAX-11||base>UINTPTR_MAX-security-11){tribes_set_last_error(87);return UINT32_MAX;}
        uint32_t attributes[3];memcpy(attributes,(const void *)(uintptr_t)(base+security),sizeof attributes);
        if(attributes[0]!=sizeof attributes){tribes_set_last_error(87);return UINT32_MAX;}
        if(creation!=3&&attributes[1]){fputs("TribesPad: unsupported CreateFile creation security descriptor\n",stderr);abort();}
        inherit=attributes[2]!=0;
    }
    char guest[PATH_MAX];if(!read_path(base,name,wide,guest))return UINT32_MAX;
    /* Apple GUI process profile: no allocated/inherited Windows console.
       Redirected standard descriptors do not establish a console attachment.
       Wine10.0 kernel32/tests/console.c:test_FreeConsole verifies error6 for
       these device opens after detachment. Alloc/AttachConsole are not exposed.
       https://learn.microsoft.com/en-us/windows/console/console-handles */
    if(!strcasecmp(guest,"CONOUT$")||!strcasecmp(guest,"CONIN$")||!strcasecmp(guest,"CON")){
        tribes_set_last_error(6);return UINT32_MAX;
    }
    int reparse=(flags&0x00200000)!=0,backup=(flags&0x02000000)!=0;
    if(creation==2&&reparse){tribes_set_last_error(87);return UINT32_MAX;}
    if(creation!=3&&(backup||reparse||!(access&0x40000000))){fputs("TribesPad: unsupported CreateFile creation access/backup mode\n",stderr);abort();}
    TribesFileOpenInfo info={0};char host[PATH_MAX],canonical[PATH_MAX];
    uint32_t data=(access&0x80000000?1:0)|(access&0x40000000?2:0);
    uint32_t rights=data|TRIBES_FILE_READ_ATTRIBUTES|(access&0x10000?TRIBES_FILE_DELETE:0)|
        (access&0x40000100?TRIBES_FILE_WRITE_ATTRIBUTES:0);
    int mode=data==3?O_RDWR:data==2?O_WRONLY:data==1?O_RDONLY:O_EVTONLY;
    int fd,created=0;
    if(creation!=3)fd=tribes_filesystem_open_create(base,guest,host,sizeof host,canonical,mode,&created);
    else{
        if(!tribes_filesystem_resolve_open(base,guest,host,sizeof host,canonical,reparse)){
            tribes_set_last_error(path_error(base,guest,errno));return UINT32_MAX;
        }
        do{fd=open(host,mode|O_CLOEXEC|O_NONBLOCK|(reparse?O_SYMLINK:O_NOFOLLOW));}while(fd<0&&errno==EINTR);
    }
    if(fd<0){int e=errno;tribes_set_last_error(e==EMFILE||e==ENFILE?4:e==ENOSPC?112:path_error(base,guest,e));return UINT32_MAX;}
    struct stat st;if(fstat(fd,&st)||(!S_ISREG(st.st_mode)&&!(backup&&S_ISDIR(st.st_mode))&&!(reparse&&S_ISLNK(st.st_mode)))){close(fd);tribes_set_last_error(5);return UINT32_MAX;}
    if(inherit&&fcntl(fd,F_SETFD,0)<0){close(fd);tribes_set_last_error(6);return UINT32_MAX;}
    /* Sequential scan is a cache hint, not a file-content or position change. */
    if(flags&0x08000000)(void)fcntl(fd,F_RDAHEAD,1);
    /* The Win32 open supplies SYNCHRONIZE/READ_ATTRIBUTES and expands generic
       masks. FileModeInformation reports only its defined mode subset, not
       backup/reparse/non-directory create options. */
    info.access=(access&0x00010180)|0x00100080;
    if(access&0x80000000)info.access|=0x00120089;
    if(access&0x40000000)info.access|=0x00120116;
    info.mode=0x20|(flags&0x08000000?4:0);
    strcpy(info.name,canonical+2);strcpy(info.host,host);
    uint32_t result=creation==2?tribes_named_file_fd_adopt_truncated(fd,rights,share,&info):tribes_named_file_fd_adopt(fd,rights,share,&info);
    if(!result){close(fd);return UINT32_MAX;}
    if(creation!=3)tribes_set_last_error(created?0:183);return result;
}
uint32_t tribes_create_file(uint64_t base,uint32_t name,uint32_t access,uint32_t share,uint32_t security,uint32_t creation,uint32_t flags,uint32_t template_handle){
    return create_file(base,name,access,share,security,creation,flags,template_handle,0);
}
uint32_t tribes_create_file_w(uint64_t base,uint32_t name,uint32_t access,uint32_t share,uint32_t security,uint32_t creation,uint32_t flags,uint32_t template_handle){
    return create_file(base,name,access,share,security,creation,flags,template_handle,1);
}
/* Match the mounted-filesystem attribute profile already used by FindFirst.
   Windows GetFileAttributes examines a final link itself; no target attributes
   are substituted. Native links lack a retained Windows file/directory reparse
   type, so those and special objects remain ERROR_NOT_SUPPORTED here. */
static uint32_t get_attributes(uint64_t base,uint32_t name,int wide) {
    char guest[PATH_MAX],host[PATH_MAX];if(!read_path(base,name,wide,guest))return UINT32_MAX;
    if(!tribes_filesystem_resolve_link(base,guest,host,sizeof host)){
        tribes_set_last_error(path_error(base,guest,errno));return UINT32_MAX;
    }
    struct stat st;if(lstat(host,&st)){
        tribes_set_last_error(path_error(base,guest,errno));return UINT32_MAX;
    }
    if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)){tribes_set_last_error(50);return UINT32_MAX;}
    uint32_t attributes=S_ISDIR(st.st_mode)?16:32;
    if(!(st.st_mode&S_IWUSR))attributes|=1;
#ifdef UF_HIDDEN
    if(st.st_flags&UF_HIDDEN)attributes|=2;
#endif
    return attributes;
}
uint32_t tribes_get_file_attributes(uint64_t base,uint32_t name){return get_attributes(base,name,0);}
uint32_t tribes_get_file_attributes_w(uint64_t base,uint32_t name){return get_attributes(base,name,1);}
/* DWORD size with optional high word. Clear LastError on success so a valid
   low word of INVALID_FILE_SIZE is distinguishable from failure. */
uint32_t tribes_get_file_size(uint64_t base,uint32_t handle,uint32_t high){
    if(high&&(high>UINT32_MAX-3||base>UINTPTR_MAX-high-3)){tribes_set_last_error(87);return UINT32_MAX;}
    int fd=tribes_fd_borrow(handle);if(fd<0)return UINT32_MAX;struct stat st;int error=fstat(fd,&st);close(fd);
    if(error){tribes_set_last_error(6);return UINT32_MAX;}if(!S_ISREG(st.st_mode)||st.st_size<0){tribes_set_last_error(1);return UINT32_MAX;}
    uint64_t size=(uint64_t)st.st_size;if(high){uint32_t value=(uint32_t)(size>>32);memcpy((void *)(uintptr_t)(base+high),&value,4);}tribes_set_last_error(0);return (uint32_t)size;
}
uint32_t tribes_set_file_pointer(uint64_t base,uint32_t handle,uint32_t low,uint32_t high,uint32_t method){
    if(method>2||(high&&(high>UINT32_MAX-3||base>UINTPTR_MAX-high-3))){tribes_set_last_error(87);return UINT32_MAX;}
    int64_t distance=(int32_t)low;if(high){uint32_t upper;memcpy(&upper,(const void *)(uintptr_t)(base+high),4);uint64_t bits=((uint64_t)upper<<32)|low;memcpy(&distance,&bits,8);}
    int fd=tribes_fd_borrow_access(handle,TRIBES_FILE_SEEK);if(fd<0)return UINT32_MAX;struct stat st;int64_t origin=0;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);tribes_set_last_error(1);return UINT32_MAX;}
    if(method==1)origin=lseek(fd,0,SEEK_CUR);else if(method==2)origin=st.st_size;
    uint64_t magnitude=0-(uint64_t)distance;
    if(origin<0||(distance<0&&(uint64_t)origin<magnitude)){close(fd);tribes_set_last_error(131);return UINT32_MAX;}
    if(distance>0&&origin>INT64_MAX-distance){close(fd);tribes_set_last_error(87);return UINT32_MAX;}
    int64_t target=origin+distance;if(!high&&(uint64_t)target>UINT32_MAX){close(fd);tribes_set_last_error(87);return UINT32_MAX;}
    off_t result=lseek(fd,(off_t)target,SEEK_SET);close(fd);if(result<0){tribes_set_last_error(87);return UINT32_MAX;}
    if(high){uint32_t upper=(uint32_t)((uint64_t)result>>32);memcpy((void *)(uintptr_t)(base+high),&upper,4);}tribes_set_last_error(0);return (uint32_t)result;
}
/* Update the retained native file, never a pathname reopened after borrowing.
   All optional guest inputs are staged before one real metadata operation.
   Darwin attrlist order is creation, modification, access. Null/zero entries
   leave that field untouched. Per-handle automatic-time suppression (-1) is
   an explicit unsupported operation, not an accepted native timestamp.
   https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfiletime */
uint32_t tribes_set_file_time(uint64_t base,uint32_t handle,uint32_t creation,uint32_t access,uint32_t write){
    uint32_t pointers[]={creation,write,access};uint64_t ticks[3]={0};
    for(unsigned i=0;i<3;i++)if(pointers[i]){
        uint32_t p=pointers[i];
        if(p<0x10000||(uint64_t)p+8>UINT64_C(0x100000000)||base>UINTPTR_MAX-p-8){tribes_set_last_error(87);return 0;}
        memcpy(ticks+i,(const void *)(uintptr_t)(base+p),8);
        if(ticks[i]==UINT64_MAX){tribes_set_last_error(50);return 0;}
        if(ticks[i]>=UINT64_C(0x8000000000000000)){tribes_set_last_error(87);return 0;}
    }
    int fd=tribes_fd_borrow_access(handle,TRIBES_FILE_WRITE_ATTRIBUTES);if(fd<0)return 0;
    struct stat st;
    if(fstat(fd,&st)){close(fd);tribes_set_last_error(6);return 0;}
    if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)){close(fd);tribes_set_last_error(50);return 0;}
    const attrgroup_t bits[]={ATTR_CMN_CRTIME,ATTR_CMN_MODTIME,ATTR_CMN_ACCTIME};
    struct attrlist attributes={0};attributes.bitmapcount=ATTR_BIT_MAP_COUNT;
    struct timespec times[3];unsigned count=0;
    for(unsigned i=0;i<3;i++)if(ticks[i]){
        attributes.commonattr|=bits[i];
        times[count++]=(struct timespec){(time_t)(ticks[i]/10000000)-INT64_C(11644473600),(long)(ticks[i]%10000000)*100};
    }
    int result=count?fsetattrlist(fd,&attributes,times,count*sizeof *times,0):0;
    int error=errno;close(fd);
    if(result){tribes_set_last_error(error==EBADF?6:error==EINVAL?87:error==ENOTSUP?50:error==EROFS?19:error==ENOSPC?112:error==EACCES||error==EPERM?5:1);return 0;}
    return 1;
}
uint32_t tribes_read_file(uint64_t base,uint32_t handle,uint32_t buffer,uint32_t count,uint32_t output,uint32_t overlapped){
    if(overlapped){fprintf(stderr,"TribesPad: overlapped ReadFile is not implemented\n");abort();}
    if(!output||output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){tribes_set_last_error(87);return 0;}
    uint32_t zero=0;memcpy((void *)(uintptr_t)(base+output),&zero,4);
    if(count&&(!buffer||buffer>UINT32_MAX-(count-1)||base>UINTPTR_MAX-buffer-(count-1))){tribes_set_last_error(87);return 0;}
    int fd=tribes_fd_borrow_access(handle,1);if(fd<0)return 0;struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);tribes_set_last_error(1);return 0;}
    /* Probe the complete destination before moving the file offset. Native
       read receives only a bounded host allocation; copied bytes cross the
       normal checked-memory boundary. Retain both output permissions through
       the synchronous transfer, including the final byte-count store. */
    void *destination=(void *)(uintptr_t)(base+buffer),*written=(void *)(uintptr_t)(base+output);
    uint32_t output_token=tribes_memory_access_begin(written,4,2);
    uint32_t buffer_token=count?tribes_memory_access_begin(destination,count,2):0;
    size_t chunk=count>65536?65536:count;
    unsigned char *copy=count?malloc(chunk):NULL;
    uint32_t total=0,error=0;
    if(count&&!copy)error=8;
    while(!error&&total<count){
        size_t requested=count-total;if(requested>chunk)requested=chunk;
        ssize_t n=read(fd,copy,requested);
        if(n<0&&errno==EINTR)continue;
        if(n<0){error=errno==EACCES?5:1117;break;}
        if(!n)break;
        memcpy((unsigned char *)destination+total,copy,(size_t)n);total+=(uint32_t)n;
    }
    free(copy);close(fd);
    if(!error)memcpy(written,&total,4);
    if(count)tribes_memory_access_end(buffer_token);
    tribes_memory_access_end(output_token);
    if(error){tribes_set_last_error(error);return 0;}return 1;
}

/* Synchronous fixed-buffer writes. Copy the complete guest source through the
   memory checker before allowing the host descriptor to observe any bytes.
   POSIX sees only owned host memory; no guest-buffer syscall exemption.
   https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile */
uint32_t tribes_write_file(uint64_t base,uint32_t handle,uint32_t buffer,uint32_t count,uint32_t output,uint32_t overlapped){
    if(overlapped){fprintf(stderr,"TribesPad: overlapped WriteFile is not implemented\n");abort();}
    if(!output||output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){tribes_set_last_error(87);return 0;}
    uint32_t total=0;memcpy((void *)(uintptr_t)(base+output),&total,4);
    if(count&&(!buffer||buffer>UINT32_MAX-(count-1)||base>UINTPTR_MAX-buffer-(count-1))){tribes_set_last_error(87);return 0;}
    int fd=tribes_fd_borrow_access(handle,2);if(fd<0)return 0;
    struct stat st;if(fstat(fd,&st)||(!S_ISREG(st.st_mode)&&!S_ISFIFO(st.st_mode)&&!S_ISCHR(st.st_mode))){close(fd);tribes_set_last_error(1);return 0;}
    int mode=fcntl(fd,F_GETFL);if(mode<0){close(fd);tribes_set_last_error(6);return 0;}
    if(S_ISFIFO(st.st_mode)&&fcntl(fd,F_SETNOSIGPIPE,1)<0){close(fd);tribes_set_last_error(1);return 0;}
    unsigned char *copy=count?malloc(count):NULL;if(count&&!copy){close(fd);tribes_set_last_error(8);return 0;}
    if(count)memcpy(copy,(const void *)(uintptr_t)(base+buffer),count);
    while(total<count){
        ssize_t n=write(fd,copy+total,(size_t)(count-total));
        if(n<0&&errno==EINTR)continue;
        if(n<0){int error=errno;if((error==EAGAIN||error==EWOULDBLOCK)&&S_ISFIFO(st.st_mode)&&(mode&O_NONBLOCK))break;
            free(copy);close(fd);tribes_set_last_error(error==EPIPE?109:error==ENOSPC?112:error==EACCES||error==EPERM?5:1117);return 0;}
        if(!n){free(copy);close(fd);tribes_set_last_error(29);return 0;}
        total+=(uint32_t)n;if(S_ISFIFO(st.st_mode)&&(mode&O_NONBLOCK))break;
    }
    free(copy);close(fd);memcpy((void *)(uintptr_t)(base+output),&total,4);return 1;
}
