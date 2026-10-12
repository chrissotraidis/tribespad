#include "guest_filesystem.h"
#include "guest_heap.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

typedef struct Mounts { uint64_t base;char volume[PATH_MAX],game[PATH_MAX],temp[PATH_MAX],cwd[PATH_MAX];int cwd_fd;struct Mounts *next; } Mounts;
static Mounts *mounts;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int directory(const char *in,char *out,dev_t *device) {
    struct stat st;if(!realpath(in,out)||stat(out,&st)||!S_ISDIR(st.st_mode))return 0;
    *device=st.st_dev;return 1;
}
int tribes_filesystem_configure(uint64_t base,const char *volume,const char *game,const char *temp) {
    if(!volume||!game||!temp){errno=EINVAL;return 0;}
    Mounts *m=calloc(1,sizeof *m);if(!m)return 0;dev_t a,b,c;
    if(!directory(volume,m->volume,&a)||!directory(game,m->game,&b)||!directory(temp,m->temp,&c)){free(m);return 0;}
    if(a!=b||a!=c){free(m);errno=EXDEV;return 0;}m->base=base;strcpy(m->cwd,"C:\\TribesPad\\Game");
    m->cwd_fd=open(m->game,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(m->cwd_fd<0){free(m);return 0;}
    pthread_mutex_lock(&lock);for(Mounts *p=mounts;p;p=p->next)if(p->base==base){pthread_mutex_unlock(&lock);close(m->cwd_fd);free(m);errno=EALREADY;return 0;}
    m->next=mounts;mounts=m;pthread_mutex_unlock(&lock);return 1;
}
static int ascii(const char *s) { for(;*s;s++)if((unsigned char)*s>=128)return 0;return 1; }
static int inside(const char *root,const char *path) {
    size_t n=strlen(root);return !strncmp(root,path,n)&&(!path[n]||path[n]=='/'||(n==1&&root[0]=='/'));
}
/* Resolve a snapshot of the process CWD. Mount paths are immutable and never
   freed during a guest process lifetime. Setter calls serialize their snapshot,
   validation and publication; ordinary resolutions use a copied CWD. */
static int resolve_path(const Mounts *m,const char *cwd,const char *guest,char *host,size_t capacity,char *canonical,int final_link) {
    if(!guest||!host||!capacity){errno=EINVAL;return 0;}
    size_t n=strnlen(guest,PATH_MAX);if(!n){errno=ENOENT;return 0;}if(n==PATH_MAX){errno=ENAMETOOLONG;return 0;}
    if(!ascii(guest)){errno=ENOTSUP;return 0;}
    char buffer[PATH_MAX];const char *p=guest;int absolute=0;
    if(p[0]=='\\'&&p[1]=='\\'){errno=ENOTSUP;return 0;}
    if(p[1]==':'){if(p[0]!='c'&&p[0]!='C'){errno=ENOENT;return 0;}p+=2;}
    if(*p=='\\'||*p=='/'){absolute=1;while(*p=='\\'||*p=='/')p++;}
    if(snprintf(buffer,sizeof buffer,"%s%s%s",absolute?"":cwd+3,absolute?"":"/",p)>=(int)sizeof buffer){errno=ENAMETOOLONG;return 0;}
    for(char *q=buffer;*q;q++)if(*q=='\\')*q='/';
    char *parts[PATH_MAX/2],*save=NULL;size_t count=0;
    for(char *q=strtok_r(buffer,"/",&save);q;q=strtok_r(NULL,"/",&save)){
        if(!strcmp(q,"."))continue;if(!strcmp(q,"..")){if(count)count--;continue;}
        if(strpbrk(q,":*?\"<>|")){errno=EINVAL;return 0;}parts[count++]=q;
    }
    const char *root=m->volume;size_t start=0;char logical[PATH_MAX]="C:\\";
    if(count>=2&&!strcasecmp(parts[0],"TribesPad")){
        if(!strcasecmp(parts[1],"Game")){root=m->game;start=2;strcpy(logical,"C:\\TribesPad\\Game");}
        else if(!strcasecmp(parts[1],"Temp")){root=m->temp;start=2;strcpy(logical,"C:\\TribesPad\\Temp");}
    }
    char path[PATH_MAX];strcpy(path,root);
    for(size_t i=start;i<count;i++){
        DIR *d=opendir(path);if(!d)return 0;struct dirent *entry;char match[NAME_MAX+1]={0};int ambiguous=0;
        while((entry=readdir(d)))if(!strcasecmp(entry->d_name,parts[i])){
            if(match[0])ambiguous=1;else strcpy(match,entry->d_name);
        }
        closedir(d);
        if((final_link==2||final_link==3)&&i+1==count&&(!match[0]||final_link==2)){
            if(match[0]){errno=EEXIST;return 0;}
            if(strlen(parts[i])>NAME_MAX){errno=ENAMETOOLONG;return 0;}
            strcpy(match,parts[i]);
        }
        if(!match[0]){errno=ENOENT;return 0;}if(ambiguous){errno=ENOTSUP;return 0;}
        size_t len=strlen(path),add=strlen(match);if(len+add+2>sizeof path){errno=ENAMETOOLONG;return 0;}
        path[len++]='/';memcpy(path+len,match,add+1);
        /* OPEN_REPARSE_POINT names the final link object, including dangling
           links. Intermediate components still resolve within this mount. */
        if(!final_link||i+1<count){char actual[PATH_MAX];if(!realpath(path,actual))return 0;
            if(!inside(root,actual)){errno=EACCES;return 0;}strcpy(path,actual);}
        len=strlen(logical);if(len+add+2>sizeof logical){errno=ENAMETOOLONG;return 0;}
        if(logical[len-1]!='\\')logical[len++]='\\';memcpy(logical+len,match,add+1);
    }
    if(strlen(path)+1>capacity){errno=ENAMETOOLONG;return 0;}strcpy(host,path);if(canonical)strcpy(canonical,logical);return 1;
}
/* Serialize case-insensitive guest creation with CWD changes. Only the missing
   final component is admitted; every parent uses normal mount confinement.
   mkdir remains authoritative for concurrent native changes and permissions. */
int tribes_filesystem_create_directory(uint64_t base,const char *guest) {
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;
    char path[PATH_MAX];int result=0,error;
    if(!m)errno=ENODEV;
    else if(resolve_path(m,m->cwd,guest,path,sizeof path,NULL,2))result=mkdir(path,0777)==0;
    error=errno;pthread_mutex_unlock(&lock);errno=error;return result;
}
/* Native O_EXCL distinguishes creation from reopening without truncating.
   Hold the CWD/creation lock through the open so guest case variants serialize.
   File sharing and truncation are applied by the handle registry afterwards. */
int tribes_filesystem_open_create(uint64_t base,const char *guest,char *host,size_t capacity,char *canonical,int mode,int *created) {
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;
    int fd=-1,error;*created=0;
    if(!m)errno=ENODEV;
    else if(resolve_path(m,m->cwd,guest,host,capacity,canonical,3)){
        do{fd=open(host,mode|O_CREAT|O_EXCL|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW,0666);}while(fd<0&&errno==EINTR);
        if(fd>=0)*created=1;
        else if(errno==EEXIST){do{fd=open(host,mode|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW);}while(fd<0&&errno==EINTR);}
    }
    error=errno;pthread_mutex_unlock(&lock);errno=error;return fd;
}
/* Host path for a leaf that may not exist yet, with the same confinement,
   case matching and name validation as CREATE_ALWAYS (resolve mode 3). */
int tribes_filesystem_resolve_create(uint64_t base,const char *guest,char *host,size_t capacity) {
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;
    int ok=0,error;
    if(!m)errno=ENODEV;else ok=resolve_path(m,m->cwd,guest,host,capacity,NULL,3);
    error=errno;pthread_mutex_unlock(&lock);errno=error;return ok;
}
int tribes_filesystem_resolve_open(uint64_t base,const char *guest,char *host,size_t capacity,char *canonical,int final_link) {
    char cwd[PATH_MAX];pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;if(m)strcpy(cwd,m->cwd);pthread_mutex_unlock(&lock);
    if(!m){errno=ENODEV;return 0;}return resolve_path(m,cwd,guest,host,capacity,canonical,final_link);
}
int tribes_filesystem_resolve(uint64_t base,const char *guest,char *host,size_t capacity) {
    return tribes_filesystem_resolve_open(base,guest,host,capacity,NULL,0);
}
int tribes_filesystem_resolve_link(uint64_t base,const char *guest,char *host,size_t capacity) {
    return tribes_filesystem_resolve_open(base,guest,host,capacity,NULL,1);
}
/* Retain a real native directory descriptor, without changing host chdir.
   Descriptor retention does not impose Windows rename/delete locking on macOS.
   ASCII existing directories only; UNC, extended/device paths are unsupported. */
uint32_t tribes_set_current_directory(uint64_t base,uint32_t name) {
    if(!name||name>UINT32_MAX-(PATH_MAX-1)||base>UINTPTR_MAX-name-(PATH_MAX-1)){tribes_set_last_error(87);return 0;}
    char guest[PATH_MAX];size_t n;
    for(n=0;n<sizeof guest;n++){guest[n]=*(const char *)(uintptr_t)(base+name+n);if(!guest[n])break;}
    if(n==sizeof guest){tribes_set_last_error(206);return 0;}
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;
    if(!m){pthread_mutex_unlock(&lock);tribes_set_last_error(3);return 0;}
    char host[PATH_MAX],logical[PATH_MAX];int fd=-1,error=0;
    if(!resolve_path(m,m->cwd,guest,host,sizeof host,logical,0))error=errno;
    else{struct stat st;if(stat(host,&st))error=errno;else if(!S_ISDIR(st.st_mode))error=ENOTDIR;else if((fd=open(host,O_RDONLY|O_DIRECTORY|O_CLOEXEC))<0)error=errno;}
    if(error){pthread_mutex_unlock(&lock);tribes_set_last_error(error==EACCES?5:error==ENOTSUP?50:error==EINVAL?123:error==ENAMETOOLONG?206:error==ENOTDIR?267:3);return 0;}
    int old=m->cwd_fd;m->cwd_fd=fd;strcpy(m->cwd,logical);pthread_mutex_unlock(&lock);close(old);return 1;
}
/* https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdiskfreespacea
   C: is a virtual volume backed by the explicitly configured native filesystem.
   Allocation unit and available blocks come from statvfs; sector size is 512. */
uint32_t tribes_disk_free_space(uint64_t base,uint32_t root,uint32_t sectors,uint32_t bytes,uint32_t available,uint32_t total) {
    uint32_t pointers[]={sectors,bytes,available,total};
    for(unsigned i=0;i<4;i++)if(!pointers[i]||pointers[i]>UINT32_MAX-3||base>UINTPTR_MAX-pointers[i]-3){tribes_set_last_error(87);return 0;}
    const char *name=root?(const char *)(uintptr_t)(base+root):"C:\\";
    if(root&&(root>UINT32_MAX-4095||base>UINTPTR_MAX-root-4095)){tribes_set_last_error(87);return 0;}
    char host[PATH_MAX];if(!tribes_filesystem_resolve(base,name,host,sizeof host)){tribes_set_last_error(errno==EACCES?5:errno==ENOTSUP?50:3);return 0;}
    struct statvfs s;if(statvfs(host,&s)){tribes_set_last_error(5);return 0;}
    uint64_t unit=s.f_frsize?s.f_frsize:s.f_bsize;
    if(!unit||unit%512||unit/512>UINT32_MAX||s.f_bavail>s.f_blocks){tribes_set_last_error(50);return 0;}
    /* Do not silently truncate a volume beyond this API's DWORD geometry. */
    if(s.f_bavail>UINT32_MAX||s.f_blocks>UINT32_MAX){tribes_set_last_error(50);return 0;}
    uint32_t values[]={(uint32_t)(unit/512),512,(uint32_t)s.f_bavail,(uint32_t)s.f_blocks};
    for(unsigned i=0;i<4;i++)memcpy((void *)(uintptr_t)(base+pointers[i]),&values[i],4);
#ifdef TRIBESPAD_DISK_OBSERVER
    extern void tribes_disk_observation(const uint32_t *,const uint32_t *);
    uint32_t args[]={root,sectors,bytes,available,total};tribes_disk_observation(args,values);
#endif
    return 1;
}

/* The current directory belongs to the guest process; it never changes host chdir.
   The initial directory is backed by the configured Game mount.
   https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getcurrentdirectory */
uint32_t tribes_get_current_directory(uint64_t base,uint32_t capacity,uint32_t output) {
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;
    if(!m){pthread_mutex_unlock(&lock);tribes_set_last_error(3);return 0;}
    uint32_t length=(uint32_t)strlen(m->cwd);
    if(capacity<=length){pthread_mutex_unlock(&lock);return length+1;}
    if(!output||output>UINT32_MAX-length||base>UINTPTR_MAX-output-length){pthread_mutex_unlock(&lock);tribes_set_last_error(87);return 0;}
    memcpy((void *)(uintptr_t)(base+output),m->cwd,length+1);pthread_mutex_unlock(&lock);return length;
}

/* Enumerate configured guest roots, not unrelated host volumes. The current
   namespace has one C: mount. Required size includes the extra MULTI_SZ NUL;
   success excludes that extra NUL but includes the per-root string terminator. */
uint32_t tribes_get_logical_drive_strings(uint64_t base,uint32_t capacity,uint32_t output) {
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;pthread_mutex_unlock(&lock);
    if(!m){tribes_set_last_error(3);return 0;}
    if(capacity<5)return 5;
    if(!output||output>UINT32_MAX-4||base>UINTPTR_MAX-output-4){tribes_set_last_error(87);return 0;}
    const unsigned char roots[]={67,58,92,0,0};memcpy((void *)(uintptr_t)(base+output),roots,sizeof roots);return 4;
}

/* C: is an explicitly configured fixed virtual volume. NULL uses its current
   directory root; absent or malformed roots have no mounted directory. */
uint32_t tribes_get_drive_type(uint64_t base,uint32_t root) {
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;pthread_mutex_unlock(&lock);
    if(!m)return 1;
    if(!root)return 3;
    if(root>UINT32_MAX-3||base>UINTPTR_MAX-root-3){tribes_set_last_error(87);return 1;}
    unsigned char name[4];memcpy(name,(const void *)(uintptr_t)(base+root),sizeof name);
    return (name[0]=='C'||name[0]=='c')&&name[1]==':'&&name[2]==92&&!name[3]?3:1;
}
