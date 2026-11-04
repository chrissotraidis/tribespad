/* Native existing-file searches. WIN32_FIND_DATAA is a guarded 320-byte x86
   layout, independent of native struct packing. Each search owns its sorted
   metadata snapshot and preserves it until FindClose. No invented 8.3 aliases. */
#include "guest_find.h"
#include "guest_filesystem.h"
#include "guest_heap.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
typedef struct Entry {unsigned char data[320];} Entry;
typedef struct Search {uint64_t base;uint32_t handle;size_t count,index;Entry *entries;struct Search *next;} Search;
static Search *searches;static uint32_t next_handle=0xe3000001;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t observe(uint32_t operation,uint32_t argument,uint32_t output,uint32_t result,const void *data){
#ifdef TRIBESPAD_FIND_OBSERVER
    extern void tribes_find_observation(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,const void *);
    tribes_find_observation(operation,argument,output,result,tribes_get_last_error(),data);
#else
    (void)operation;(void)argument;(void)output;(void)data;
#endif
    return result;
}
static int valid_output(uint64_t base,uint32_t output){return output&&output<=UINT32_MAX-319&&base<=UINTPTR_MAX-output-319;}
static unsigned char lower(unsigned char c){return c>='A'&&c<='Z'?c+32:c;}
/* Star/literal ASCII matching, including the Win32 *.* all-files convention.
   DOS question-mark/short-name behavior remains a strict unsupported gate. */
static int match(const char *pattern,const char *name){
    if(!strcmp(pattern,"*.*"))pattern="*";
    const char *star=NULL,*retry=NULL;
    while(*name){if(*pattern=='*'){star=pattern++;retry=name;}else if(lower((unsigned char)*pattern)==lower((unsigned char)*name)){pattern++;name++;}else if(star){pattern=star+1;name=++retry;}else return 0;}
    while(*pattern=='*')pattern++;return !*pattern||!strcmp(pattern,".*");
}
static int compare(const void *a,const void *b){const char *x=(const char *)((const Entry *)a)->data+44,*y=(const char *)((const Entry *)b)->data+44;int order=strcasecmp(x,y);return order?order:strcmp(x,y);}
/* Sort the private directory snapshot with direct checked calls. Passing a
   comparator through libc qsort would escape the verified callback boundary. */
static void sort_entries(Entry *entries,size_t count){
    for(size_t i=1;i<count;i++){
        Entry value=entries[i];size_t j=i;
        while(j&&compare(&entries[j-1],&value)>0){entries[j]=entries[j-1];j--;}
        entries[j]=value;
    }
}
static void put32(unsigned char *data,size_t offset,uint32_t value){memcpy(data+offset,&value,4);}
static int put_time(unsigned char *data,size_t offset,struct timespec time){
    if(time.tv_sec<INT64_C(-11644473600)||time.tv_nsec<0||time.tv_nsec>=1000000000)return 0;
    uint64_t seconds=time.tv_sec>=0?(uint64_t)time.tv_sec+UINT64_C(11644473600):(uint64_t)(time.tv_sec+INT64_C(11644473600));if(seconds>(UINT64_MAX-(uint64_t)time.tv_nsec/100)/10000000)return 0;
    uint64_t value=seconds*10000000+(uint64_t)time.tv_nsec/100;memcpy(data+offset,&value,8);return 1;
}
static uint32_t path_error(void){return errno==EACCES?5:errno==ENOTSUP?50:errno==ENAMETOOLONG?206:3;}
/* Folder snapshots: every name in a folder with its find data, or the error a
   search that matches it reports. Tribes: Vengeance lists its 200 multiplayer
   maps one at a time (each "next map" lists the whole folder again), and each
   name took two system calls: the Host menu froze for seconds. A snapshot
   serves searches of its folder for 100 ms, and until TribesPad changes a file
   (tribes_filesystem_changed), as the folder listings in guest_filesystem.c do. */
enum {SNAPSHOT_FRESH_NS=100000000,SNAPSHOTS_KEPT=32};
typedef struct Item {char name[260];uint32_t error;Entry entry;} Item;
typedef struct Snapshot {struct Snapshot *next;char host[PATH_MAX];unsigned generation;uint64_t taken;size_t count;Item *items;} Snapshot;
static Snapshot *snapshots;static pthread_mutex_t snapshot_lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t now_ns(void){struct timespec t;clock_gettime(CLOCK_UPTIME_RAW,&t);return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
/* One name: its find data, or the error a search matching it fails with. */
static uint32_t describe(uint64_t base,const char *directory,const char *host,const char *name,Entry *entry){
    size_t n=strlen(name);if(n>=260)return 206;for(size_t i=0;i<n;i++)if((unsigned char)name[i]>=128)return 50;
    char child[PATH_MAX],resolved[PATH_MAX],native[PATH_MAX];if(snprintf(child,sizeof child,"%s/%s",directory,name)>=(int)sizeof child||snprintf(native,sizeof native,"%s/%s",host,name)>=(int)sizeof native)return 206;
    if(!tribes_filesystem_resolve(base,child,resolved,sizeof resolved))return path_error();
    struct stat st,link;if(lstat(native,&link)||stat(resolved,&st))return 5;if(S_ISLNK(link.st_mode)||(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode)))return 50;
    memset(entry,0,sizeof *entry);uint32_t attributes=S_ISDIR(st.st_mode)?16:32;if(!(st.st_mode&S_IWUSR))attributes|=1;
#ifdef UF_HIDDEN
    if(st.st_flags&UF_HIDDEN)attributes|=2;
#endif
    put32(entry->data,0,attributes);if(!put_time(entry->data,4,st.st_birthtimespec)||!put_time(entry->data,12,st.st_atimespec)||!put_time(entry->data,20,st.st_mtimespec))return 50;
    uint64_t size=S_ISREG(st.st_mode)?(uint64_t)st.st_size:0;put32(entry->data,28,(uint32_t)(size>>32));put32(entry->data,32,(uint32_t)size);memcpy(entry->data+44,name,n+1);
    return 0;
}
/* The folder's snapshot, new if needed (snapshot_lock held). 0: error set. */
static Snapshot *snapshot(uint64_t base,const char *directory,const char *host,uint32_t *error){
    unsigned generation=tribes_filesystem_generation();uint64_t now=now_ns();size_t kept=0;
    Snapshot **link=&snapshots;
    while(*link){Snapshot *s=*link;
        if(!strcmp(s->host,host)){if(s->generation==generation&&now-s->taken<SNAPSHOT_FRESH_NS)return s;*link=s->next;free(s->items);free(s);continue;}
        if(++kept>=SNAPSHOTS_KEPT){*link=s->next;free(s->items);free(s);continue;}
        link=&s->next;}
    DIR *stream=opendir(host);if(!stream){*error=path_error();return NULL;}
    Snapshot *s=calloc(1,sizeof *s);if(!s){closedir(stream);*error=8;return NULL;}
    strcpy(s->host,host);s->generation=generation;s->taken=now;size_t capacity=0;struct dirent *item;
    for(;;){errno=0;item=readdir(stream);if(!item){if(errno){*error=5;goto failed;}break;}
        if(s->count==capacity){size_t grown=capacity?capacity*2:16;Item *items=realloc(s->items,grown*sizeof *items);if(!items){*error=8;goto failed;}s->items=items;capacity=grown;}
        Item *it=&s->items[s->count++];snprintf(it->name,sizeof it->name,"%s",item->d_name);
        it->error=describe(base,directory,host,item->d_name,&it->entry);}
    closedir(stream);s->next=snapshots;snapshots=s;return s;
failed:
    closedir(stream);free(s->items);free(s);return NULL;
}
uint32_t tribes_find_first(uint64_t base,uint32_t pattern_pointer,uint32_t output){
    uint32_t error=87,result=UINT32_MAX;Search *s=NULL;
    if(!pattern_pointer||!valid_output(base,output))goto failed;
    char request[PATH_MAX];size_t length=0;
    for(;length<sizeof request;length++){if(pattern_pointer>UINT32_MAX-length||base>UINTPTR_MAX-pattern_pointer-length)goto failed;request[length]=*(const char *)(uintptr_t)(base+pattern_pointer+length);if(!request[length])break;}
    if(!length||length==sizeof request){error=length?206:2;goto failed;}
    for(size_t i=0;i<length;i++){if((unsigned char)request[i]>=128){error=50;goto failed;}if(request[i]=='\\')request[i]='/';}
    char directory[PATH_MAX],pattern[PATH_MAX];char *slash=strrchr(request,'/'),*leaf=request;
    if(slash){leaf=slash+1;size_t span=(size_t)(slash-request);if(!span||(span==2&&request[1]==':'))span++;memcpy(directory,request,span);directory[span]=0;}
    else if(request[1]==':'){memcpy(directory,request,2);directory[2]=0;leaf=request+2;}
    else strcpy(directory,".");
    strcpy(pattern,leaf);if(!*pattern){error=2;goto failed;}
    if(strchr(pattern,'?')||pattern[strlen(pattern)-1]=='.'){fprintf(stderr,"TribesPad: DOS question-mark/trailing-period file searches are not implemented\n");abort();}
    if(strpbrk(pattern,":\"<>|")||strpbrk(directory,"*?")){error=123;goto failed;}
    char host[PATH_MAX];if(!tribes_filesystem_resolve(base,directory,host,sizeof host)){error=path_error();goto failed;}
    s=calloc(1,sizeof *s);if(!s){error=8;goto failed;}s->base=base;size_t capacity=0;
    pthread_mutex_lock(&snapshot_lock);
    Snapshot *snap=snapshot(base,directory,host,&error);
    for(size_t i=0;snap&&i<snap->count;i++){Item *it=&snap->items[i];if(!match(pattern,it->name))continue;
        if(it->error){error=it->error;snap=NULL;break;}
        if(s->count==capacity){if(capacity>SIZE_MAX/2/sizeof(Entry)){error=8;snap=NULL;break;}size_t new_capacity=capacity?capacity*2:16;Entry *entries=realloc(s->entries,new_capacity*sizeof *entries);if(!entries){error=8;snap=NULL;break;}s->entries=entries;capacity=new_capacity;}
        s->entries[s->count++]=it->entry;}
    pthread_mutex_unlock(&snapshot_lock);
    if(!snap)goto failed;
    if(!s->count){error=2;goto failed;}sort_entries(s->entries,s->count);
    for(size_t i=1;i<s->count;i++)if(!strcasecmp((char *)s->entries[i-1].data+44,(char *)s->entries[i].data+44)){error=50;goto failed;}
    pthread_mutex_lock(&lock);if(next_handle>=0xe4000000){pthread_mutex_unlock(&lock);error=8;goto failed;}s->handle=next_handle++;s->index=1;s->next=searches;searches=s;result=s->handle;memcpy((void *)(uintptr_t)(base+output),s->entries[0].data,320);
    observe(1,pattern_pointer,output,result,s->entries[0].data);pthread_mutex_unlock(&lock);return result;
failed:
    if(s){free(s->entries);free(s);}tribes_set_last_error(error);return observe(1,pattern_pointer,output,UINT32_MAX,NULL);
}
uint32_t tribes_find_next(uint64_t base,uint32_t handle,uint32_t output){
    if(!valid_output(base,output)){tribes_set_last_error(87);return observe(2,handle,output,0,NULL);}
    pthread_mutex_lock(&lock);Search *s=searches;while(s&&(s->base!=base||s->handle!=handle))s=s->next;
    if(!s||s->index==s->count){uint32_t error=s?18:6;pthread_mutex_unlock(&lock);tribes_set_last_error(error);return observe(2,handle,output,0,NULL);}
    const void *data=s->entries[s->index++].data;memcpy((void *)(uintptr_t)(base+output),data,320);observe(2,handle,output,1,data);pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_find_close(uint64_t base,uint32_t handle){
    pthread_mutex_lock(&lock);Search **link=&searches;while(*link&&((*link)->base!=base||(*link)->handle!=handle))link=&(*link)->next;Search *s=*link;
    if(!s){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return observe(3,handle,0,0,NULL);}*link=s->next;free(s->entries);free(s);pthread_mutex_unlock(&lock);return observe(3,handle,0,1,NULL);
}
