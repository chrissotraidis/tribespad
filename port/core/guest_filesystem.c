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
#include <time.h>

typedef struct Mounts { uint64_t base;char volume[PATH_MAX],game[PATH_MAX],temp[PATH_MAX],content[PATH_MAX],cwd[PATH_MAX];int cwd_fd;struct Mounts *next; } Mounts;
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
/* Optional: C:\TribesPad\Game\Content from another folder, confined to it. On iOS
   Tribes: Vengeance reads its gigabytes of content from the app bundle in place,
   while the rest of the game folder stays writable in Application Support. */
int tribes_filesystem_configure_content(uint64_t base,const char *content) {
    char path[PATH_MAX];dev_t device;if(!content||!directory(content,path,&device))return 0;
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;
    if(m)strcpy(m->content,path);pthread_mutex_unlock(&lock);if(!m)errno=ENODEV;return m!=NULL;
}
static int ascii(const char *s) { for(;*s;s++)if((unsigned char)*s>=128)return 0;return 1; }
static int inside(const char *root,const char *path) {
    size_t n=strlen(root);return !strncmp(root,path,n)&&(!path[n]||path[n]=='/'||(n==1&&root[0]=='/'));
}
/* Folder listings for case-insensitive matching, cached per host folder. A
   lookup used to open and read every folder on the path and realpath() each
   step: dozens of system calls per GetFileAttributes, and Tribes: Vengeance
   checks thousands of files at start-up (most of a minute on an iPad).
   It also reads its INI files through GetFileAttributes at nearly every
   setting it reads: hundreds of thousands of checks while a map loads.
   A listing, and the attributes of a name in it, are used without a system
   call for 100 ms after they were checked. Then one stat of the folder shows
   whether it changed (change time, inode), and it is read again if so. When
   TribesPad itself creates, removes, renames or changes the attributes of
   something (tribes_filesystem_changed), everything is checked again at once;
   a change made by another program shows within 100 ms. */
enum {LISTING_FRESH_NS=100000000};
typedef struct {char *name;unsigned char type;uint32_t flags;mode_t mode;unsigned stat_generation;uint64_t stat_time;} ListedName;
typedef struct Listing {struct Listing *next;char *path;struct timespec changed;ino_t ino;dev_t dev;unsigned generation;uint64_t checked;size_t count;ListedName *names;} Listing;
static Listing *listings[2048];
static unsigned listing_generation;
static pthread_mutex_t listing_lock=PTHREAD_MUTEX_INITIALIZER;
void tribes_filesystem_changed(void){pthread_mutex_lock(&listing_lock);listing_generation++;pthread_mutex_unlock(&listing_lock);}
static unsigned listing_slot(const char *s){unsigned h=2166136261u;while(*s)h=(h^(unsigned char)*s++)*16777619u;return h&2047u;}
/* Sorted and searched here: the guarded runtime admits no callback-taking library calls (qsort, bsearch). */
static void listed_sort(ListedName *names,size_t count){
    for(size_t gap=count/2;gap;gap/=2)for(size_t i=gap;i<count;i++){
        ListedName v=names[i];size_t j=i;
        while(j>=gap&&strcasecmp(names[j-gap].name,v.name)>0){names[j]=names[j-gap];j-=gap;}
        names[j]=v;}
}
static ListedName *listed_find(ListedName *names,size_t count,const char *name){
    size_t lo=0,hi=count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;int c=strcasecmp(names[mid].name,name);if(!c)return names+mid;if(c<0)lo=mid+1;else hi=mid;}
    return NULL;
}
static int listing_read(Listing *l,const struct stat *st){
    DIR *d=opendir(l->path);if(!d)return 0;
    size_t count=0,capacity=32;ListedName *names=malloc(capacity*sizeof *names);struct dirent *entry;
    while(names&&(entry=readdir(d))){
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        if(count==capacity){ListedName *grown=realloc(names,2*capacity*sizeof *names);if(!grown)break;names=grown;capacity*=2;}
        memset(names+count,0,sizeof *names);
        if(!(names[count].name=strdup(entry->d_name)))break;names[count++].type=entry->d_type;
    }
    closedir(d);if(!names){errno=ENOMEM;return 0;}
    listed_sort(names,count);
    for(size_t i=0;i<l->count;i++)free(l->names[i].name);free(l->names);
    l->names=names;l->count=count;l->changed=st->st_ctimespec;l->ino=st->st_ino;l->dev=st->st_dev;return 1;
}
/* One stat of the folder; read again if it changed. 0: the folder cannot be read (errno). */
static int listing_check(Listing *l,uint64_t now){
    struct stat st;if(stat(l->path,&st))return 0;if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return 0;}
    if(!l->names||st.st_ctimespec.tv_sec!=l->changed.tv_sec||st.st_ctimespec.tv_nsec!=l->changed.tv_nsec||st.st_ino!=l->ino||st.st_dev!=l->dev)
        if(!listing_read(l,&st))return 0;
    l->generation=listing_generation;l->checked=now;return 1;
}
/* 1: name is in folder (match holds its case, ambiguous if more than one), 0: it is not, -1: the folder cannot be read (errno).
   verify checks the folder first, for a name about to be created. st, if given,
   receives the name's lstat mode and flags (-1 and errno if lstat fails). */
static int listing_find(const char *folder,int verify,const char *name,char *match,unsigned char *type,int *ambiguous,struct stat *st){
    pthread_mutex_lock(&listing_lock);
    unsigned slot=listing_slot(folder);Listing *l=listings[slot];while(l&&strcmp(l->path,folder))l=l->next;
    struct timespec clock;clock_gettime(CLOCK_UPTIME_RAW,&clock);uint64_t now=(uint64_t)clock.tv_sec*1000000000u+(uint64_t)clock.tv_nsec;int result=0;
    if(!l){l=calloc(1,sizeof *l);if(!l||!(l->path=strdup(folder))){free(l);pthread_mutex_unlock(&listing_lock);errno=ENOMEM;return -1;}
        if(!listing_check(l,now)){int e=errno;free(l->path);free(l);pthread_mutex_unlock(&listing_lock);errno=e;return -1;}
        l->next=listings[slot];listings[slot]=l;}
    else if(verify||l->generation!=listing_generation||now-l->checked>=LISTING_FRESH_NS){
        if(!listing_check(l,now)){pthread_mutex_unlock(&listing_lock);return -1;}}
    ListedName *hit=listed_find(l->names,l->count,name);
    if(hit){
        ListedName *first=hit,*last=hit,*end=l->names+l->count;
        while(first>l->names&&!strcasecmp(first[-1].name,name))first--;
        while(last+1<end&&!strcasecmp(last[1].name,name))last++;
        strcpy(match,first->name);*type=first->type;*ambiguous=first!=last;result=1;
        if(st&&(first->stat_generation!=listing_generation||!first->stat_time||now-first->stat_time>=LISTING_FRESH_NS)){
            char path[PATH_MAX];struct stat full;
            if(snprintf(path,sizeof path,"%s/%s",folder,first->name)>=(int)sizeof path){errno=ENAMETOOLONG;result=-1;}
            else if(lstat(path,&full)){result=-1;l->checked=0;}   /* gone since: read the folder again next time */
            else{first->mode=full.st_mode;first->flags=full.st_flags;first->stat_time=now;first->stat_generation=listing_generation;}
        }
        if(st&&result==1){memset(st,0,sizeof *st);st->st_mode=first->mode;st->st_flags=first->flags;}
    }
    pthread_mutex_unlock(&listing_lock);return result;
}
/* Resolve a snapshot of the process CWD. Mount paths are immutable and never
   freed during a guest process lifetime. Setter calls serialize their snapshot,
   validation and publication; ordinary resolutions use a copied CWD. */
static int resolve_path(const Mounts *m,const char *cwd,const char *guest,char *host,size_t capacity,char *canonical,int final_link,struct stat *st) {
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
        if(root==m->game&&m->content[0]&&count>=3&&!strcasecmp(parts[2],"Content")){root=m->content;start=3;strcpy(logical,"C:\\TribesPad\\Game\\Content");}
    }
    char path[PATH_MAX];strcpy(path,root);
    for(size_t i=start;i<count;i++){
        char match[NAME_MAX+1]={0};int ambiguous=0;unsigned char type=DT_UNKNOWN;
        int found=listing_find(path,(final_link==2||final_link==3)&&i+1==count,parts[i],match,&type,&ambiguous,i+1==count?st:NULL);
        if(found<0)return 0;if(!found)match[0]=0;
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
        /* Only a link (or an entry of unknown type) can lead elsewhere: a plain
           file or folder named by its listing keeps the path canonical. */
        if((!final_link||i+1<count)&&(type==DT_LNK||type==DT_UNKNOWN)){char actual[PATH_MAX];if(!realpath(path,actual))return 0;
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
    else if(resolve_path(m,m->cwd,guest,path,sizeof path,NULL,2,NULL))result=mkdir(path,0777)==0;
    error=errno;pthread_mutex_unlock(&lock);if(result)tribes_filesystem_changed();errno=error;return result;
}
/* Native O_EXCL distinguishes creation from reopening without truncating.
   Hold the CWD/creation lock through the open so guest case variants serialize.
   File sharing and truncation are applied by the handle registry afterwards. */
int tribes_filesystem_open_create(uint64_t base,const char *guest,char *host,size_t capacity,char *canonical,int mode,int *created) {
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;
    int fd=-1,error;*created=0;
    if(!m)errno=ENODEV;
    else if(resolve_path(m,m->cwd,guest,host,capacity,canonical,3,NULL)){
        do{fd=open(host,mode|O_CREAT|O_EXCL|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW,0666);}while(fd<0&&errno==EINTR);
        if(fd>=0)*created=1;
        else if(errno==EEXIST){do{fd=open(host,mode|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW);}while(fd<0&&errno==EINTR);}
    }
    error=errno;pthread_mutex_unlock(&lock);if(*created)tribes_filesystem_changed();errno=error;return fd;
}
/* Host path for a leaf that may not exist yet, with the same confinement,
   case matching and name validation as CREATE_ALWAYS (resolve mode 3). */
int tribes_filesystem_resolve_create(uint64_t base,const char *guest,char *host,size_t capacity) {
    pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;
    int ok=0,error;
    if(!m)errno=ENODEV;else ok=resolve_path(m,m->cwd,guest,host,capacity,NULL,3,NULL);
    error=errno;pthread_mutex_unlock(&lock);errno=error;return ok;
}
static int resolve_snapshot(uint64_t base,const char *guest,char *host,size_t capacity,char *canonical,int final_link,struct stat *st) {
    char cwd[PATH_MAX];pthread_mutex_lock(&lock);Mounts *m=mounts;while(m&&m->base!=base)m=m->next;if(m)strcpy(cwd,m->cwd);pthread_mutex_unlock(&lock);
    if(!m){errno=ENODEV;return 0;}return resolve_path(m,cwd,guest,host,capacity,canonical,final_link,st);
}
int tribes_filesystem_resolve_open(uint64_t base,const char *guest,char *host,size_t capacity,char *canonical,int final_link) {
    return resolve_snapshot(base,guest,host,capacity,canonical,final_link,NULL);
}
int tribes_filesystem_resolve_stat(uint64_t base,const char *guest,char *host,size_t capacity,struct stat *st) {
    memset(st,0,sizeof *st);if(!resolve_snapshot(base,guest,host,capacity,NULL,1,st))return 0;
    return st->st_mode||!lstat(host,st);   /* a mount's own folder has no listing entry */
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
    if(!resolve_path(m,m->cwd,guest,host,sizeof host,logical,0,NULL))error=errno;
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
