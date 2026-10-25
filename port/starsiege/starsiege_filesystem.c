/* Starsiege's copy of TribesPad's guest filesystem, with two caches for the
   case-insensitive path lookup (port/core/guest_filesystem.c, unchanged and
   shared with Tribes 2, which keeps the uncached build its receipts check).

   Windows names ignore case, so every file the game opens is resolved by
   reading each folder on its path and matching names. Starsiege looks files
   up across a search path of about sixty folders, so a mission load listed
   the same folders thousands of times: 12 to 14 second frames on the iPad.

   - Folder listings are kept and reused until the folder's modification time
     (or identity) changes, which happens whenever an entry is added, removed
     or renamed. A changed folder is listed again.
   - realpath() is skipped for a known real folder plus one name that lstat()
     shows is not a symbolic link: that path is already real. Anything else
     goes to realpath() as before.
   Both give exactly the answers the uncached code gives. */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct SSListing { char *path,*names;size_t size;int count;struct timespec mtime;dev_t device;ino_t inode;struct SSListing *next; } SSListing;
typedef struct SSKnown { char *path;struct SSKnown *next; } SSKnown;
typedef struct SSHandle { char *names;size_t size,at;struct dirent entry; } SSHandle;
enum { SS_BUCKETS=1024 };
static SSListing *ss_listings[SS_BUCKETS];
static SSKnown *ss_known[SS_BUCKETS];
static pthread_mutex_t ss_lock=PTHREAD_MUTEX_INITIALIZER;

static uint32_t ss_hash(const char *s,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;i++){h^=(unsigned char)s[i];h*=16777619u;}return h%SS_BUCKETS;}
static int ss_is_known(const char *path,size_t n){
    int found=0;pthread_mutex_lock(&ss_lock);
    for(SSKnown *k=ss_known[ss_hash(path,n)];k;k=k->next)if(strlen(k->path)==n&&!memcmp(k->path,path,n)){found=1;break;}
    pthread_mutex_unlock(&ss_lock);return found;
}
static void ss_add_known(const char *path){
    size_t n=strlen(path);if(ss_is_known(path,n))return;
    SSKnown *k=malloc(sizeof *k);if(!k)return;k->path=strdup(path);if(!k->path){free(k);return;}
    pthread_mutex_lock(&ss_lock);uint32_t h=ss_hash(path,n);k->next=ss_known[h];ss_known[h]=k;pthread_mutex_unlock(&ss_lock);
}
static char *ss_realpath(const char *in,char *out){
    if(!in){errno=EINVAL;return NULL;}
    if(in&&out&&in[0]=='/'){
        const char *slash=strrchr(in,'/'),*name=slash+1;size_t parent=(size_t)(slash-in),length=strlen(in);
        if(parent&&*name&&strcmp(name,".")&&strcmp(name,"..")&&length<PATH_MAX&&ss_is_known(in,parent)){
            struct stat st;if(!lstat(in,&st)&&!S_ISLNK(st.st_mode)){memcpy(out,in,length+1);if(S_ISDIR(st.st_mode))ss_add_known(out);return out;}
        }
    }
    char *result=realpath(in,out);
    if(result){struct stat st;if(!stat(result,&st)&&S_ISDIR(st.st_mode))ss_add_known(result);}
    return result;
}
/* A private copy of the folder's names for this handle, so a listing replaced
   by another thread never changes under a reader. */
static DIR *ss_opendir(const char *path){
    struct stat st;if(stat(path,&st))return NULL;if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return NULL;}
    SSHandle *h=calloc(1,sizeof *h);if(!h){errno=ENOMEM;return NULL;}
    size_t n=strlen(path);uint32_t bucket=ss_hash(path,n);
    pthread_mutex_lock(&ss_lock);
    for(SSListing *l=ss_listings[bucket];l;l=l->next)if(!strcmp(l->path,path)){
        if(l->device==st.st_dev&&l->inode==st.st_ino&&l->mtime.tv_sec==st.st_mtimespec.tv_sec&&l->mtime.tv_nsec==st.st_mtimespec.tv_nsec){
            h->names=malloc(l->size?l->size:1);if(h->names){memcpy(h->names,l->names,l->size);h->size=l->size;}
        }
        break;
    }
    pthread_mutex_unlock(&ss_lock);
    if(h->names)return (DIR *)h;
    DIR *d=opendir(path);if(!d){int e=errno;free(h);errno=e;return NULL;}
    size_t capacity=4096,size=0;int count=0;char *names=malloc(capacity);struct dirent *entry;
    while(names&&(entry=readdir(d))){
        size_t len=strlen(entry->d_name)+1;
        if(size+len>capacity){while(size+len>capacity)capacity*=2;char *grown=realloc(names,capacity);if(!grown){free(names);names=NULL;break;}names=grown;}
        memcpy(names+size,entry->d_name,len);size+=len;count++;
    }
    closedir(d);if(!names){free(h);errno=ENOMEM;return NULL;}
    h->names=malloc(size?size:1);if(!h->names){free(names);free(h);errno=ENOMEM;return NULL;}
    memcpy(h->names,names,size);h->size=size;
    pthread_mutex_lock(&ss_lock);
    SSListing *l=NULL;for(SSListing *p=ss_listings[bucket];p;p=p->next)if(!strcmp(p->path,path)){l=p;break;}
    if(!l&&(l=calloc(1,sizeof *l))&&(l->path=strdup(path))){l->next=ss_listings[bucket];ss_listings[bucket]=l;}
    else if(l&&!l->path){free(l);l=NULL;}
    if(l){free(l->names);l->names=names;l->size=size;l->count=count;l->mtime=st.st_mtimespec;l->device=st.st_dev;l->inode=st.st_ino;names=NULL;}
    pthread_mutex_unlock(&ss_lock);
    free(names);return (DIR *)h;
}
static struct dirent *ss_readdir(DIR *d){
    SSHandle *h=(SSHandle *)d;if(h->at>=h->size)return NULL;
    const char *name=h->names+h->at;size_t len=strlen(name);h->at+=len+1;
    memset(&h->entry,0,sizeof h->entry);h->entry.d_ino=1;h->entry.d_type=DT_UNKNOWN;
    if(len>=sizeof h->entry.d_name)len=sizeof h->entry.d_name-1;
    memcpy(h->entry.d_name,name,len);h->entry.d_name[len]=0;h->entry.d_namlen=(__uint16_t)len;h->entry.d_reclen=sizeof h->entry;
    return &h->entry;
}
static int ss_closedir(DIR *d){SSHandle *h=(SSHandle *)d;free(h->names);free(h);return 0;}

#define opendir ss_opendir
#define readdir ss_readdir
#define closedir ss_closedir
#define realpath ss_realpath
#include "../core/guest_filesystem.c"

