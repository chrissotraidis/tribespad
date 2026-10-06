/* Native descriptor-backed Win32 handles; guest values never contain host pointers.
   Standard descriptors are duplicated, so guest CloseHandle cannot close the
   application's original diagnostic/UI descriptors. Registry borrows survive close.
   Invalid std selector behavior also checked against wine-mirror/wine
   dlls/kernel32/tests/console.c, test_GetSetStdHandle (Windows conformance tests).
   Contracts: https://learn.microsoft.com/en-us/windows/console/getstdhandle
   https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype */
#include "native_handles.h"
#include "guest_heap.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
typedef struct EventWaiter {int ready;struct EventWaiter *next;} EventWaiter;
typedef struct Event {uint32_t refs,handles;int manual,signaled;pthread_cond_t changed;EventWaiter *waiters;} Event;
static void drop_event(Event *e){if(!--e->refs){if(pthread_cond_destroy(&e->changed))abort();free(e);}}
typedef struct Semaphore {uint32_t count,maximum,refs,handles;pthread_cond_t changed;} Semaphore;
typedef struct FileShare {dev_t device;ino_t inode;uint32_t access,share,refs;TribesFileOpenInfo info;} FileShare;
typedef struct CompletionPacket {uint32_t bytes,key,overlapped,status,error;uint64_t base;struct CompletionPacket *next;} CompletionPacket;
typedef struct CompletionPort {uint32_t refs;int open;uint64_t consumer;pthread_cond_t changed;CompletionPacket *head,*tail;} CompletionPort;
struct TribesCompletionBinding {CompletionPort *port;uint32_t key;};
static void drop_completion(CompletionPort *p){if(!--p->refs){while(p->head){CompletionPacket *q=p->head;p->head=q->next;free(q);}pthread_cond_destroy(&p->changed);free(p);}}
struct TribesThreadObject {uint32_t refs,id,suspended,code;int started,finished,native_ready;pthread_t native;pthread_cond_t changed;};
static _Thread_local TribesThreadObject *current_thread_object;
static uint32_t live_threads;
static void drop_thread(TribesThreadObject *t){if(!--t->refs){if(pthread_cond_destroy(&t->changed))abort();--live_threads;free(t);}}
typedef struct LocalMutex {uint32_t refs,handles,depth;int abandoned;pthread_t owner;pthread_cond_t changed;struct LocalMutex *next;} LocalMutex;
static LocalMutex *local_mutexes;
typedef struct Handle { uint32_t value;int fd,instance_mutex;pthread_t initial_owner;FileShare *file;LocalMutex *mutex;Event *event;Semaphore *semaphore;CompletionPort *completion;TribesThreadObject *thread;struct Handle *next; } Handle;
static void drop_semaphore(Semaphore *s){if(!--s->refs){pthread_cond_destroy(&s->changed);free(s);}}
static Handle *handles;
static uint32_t next_handle=0xe1000001;
static uint32_t standard[3];
static pthread_mutex_t registry=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t multi_changed=PTHREAD_COND_INITIALIZER;
static pthread_once_t startup=PTHREAD_ONCE_INIT;
static char mutex_directory[PATH_MAX];
/* Registry lock protects ownership, recursive depth, waiters and handle refs.
   A thread-specific destructor covers native callers; guest exit also abandons
   before publishing its thread object as signaled. No pthread ID is retained
   after its owner terminates. */
static void abandon_mutexes_locked(void){
    for(LocalMutex *m=local_mutexes;m;m=m->next)if(m->depth&&pthread_equal(m->owner,pthread_self())){
        m->depth=0;m->abandoned=1;pthread_cond_broadcast(&m->changed);pthread_cond_broadcast(&multi_changed);
    }
}
static void mutex_owner_exit(void *unused){(void)unused;pthread_mutex_lock(&registry);abandon_mutexes_locked();pthread_mutex_unlock(&registry);}
static pthread_key_t mutex_owner_key;
static pthread_once_t mutex_owner_once=PTHREAD_ONCE_INIT;
static void mutex_owner_initialize(void){if(pthread_key_create(&mutex_owner_key,mutex_owner_exit))abort();}
static int register_mutex_owner(void){pthread_once(&mutex_owner_once,mutex_owner_initialize);return pthread_setspecific(mutex_owner_key,(void *)1)==0;}
static void drop_mutex(LocalMutex *m){
    if(!--m->refs){LocalMutex **p=&local_mutexes;while(*p&&*p!=m)p=&(*p)->next;if(!*p)abort();*p=m->next;if(pthread_cond_destroy(&m->changed))abort();free(m);}
}
static uint32_t create_local_mutex(uint32_t attributes,uint32_t initial){
    if(attributes){fputs("TribesPad: unsupported mutex security attributes\n",stderr);abort();}
    if(initial&&!register_mutex_owner()){tribes_set_last_error(8);return 0;}
    LocalMutex *m=calloc(1,sizeof *m);Handle *h=calloc(1,sizeof *h);
    if(!m||!h){free(m);free(h);tribes_set_last_error(8);return 0;}
    if(pthread_cond_init(&m->changed,NULL)){free(m);free(h);tribes_set_last_error(8);return 0;}
    m->refs=m->handles=1;if(initial){m->depth=1;m->owner=pthread_self();}h->fd=-1;h->mutex=m;
    pthread_mutex_lock(&registry);
    if(next_handle>=0xe2000000){pthread_mutex_unlock(&registry);pthread_cond_destroy(&m->changed);free(m);free(h);tribes_set_last_error(8);return 0;}
    m->next=local_mutexes;local_mutexes=m;h->value=next_handle++;h->next=handles;handles=h;uint32_t result=h->value;
    pthread_mutex_unlock(&registry);return result;
}
/* https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasemutex */
uint32_t tribes_release_mutex(uint32_t handle){
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;
    if(h&&h->instance_mutex){pthread_mutex_unlock(&registry);fputs("TribesPad: unsupported named instance mutex release\n",stderr);abort();}
    LocalMutex *m=h?h->mutex:NULL;
    if(!m){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return 0;}
    if(!m->depth||!pthread_equal(m->owner,pthread_self())){pthread_mutex_unlock(&registry);tribes_set_last_error(288);return 0;}
    if(!--m->depth){pthread_cond_broadcast(&m->changed);pthread_cond_broadcast(&multi_changed);}pthread_mutex_unlock(&registry);return 1;
}

/* The configured virtual Windows session owns this namespace. Persistent empty
   files are lock identities, not existence flags: the final descriptor close
   (including process death) removes the actual kernel lifetime lock. */
int tribes_named_mutex_configure(const char *directory){
    char path[PATH_MAX];struct stat st;
    if(!directory||!realpath(directory,path)||stat(path,&st)||!S_ISDIR(st.st_mode))return 0;
    pthread_mutex_lock(&registry);int ok=!mutex_directory[0]||!strcmp(path,mutex_directory);
    if(ok)strcpy(mutex_directory,path);pthread_mutex_unlock(&registry);return ok;
}
static int lock_file(int fd,int operation){int result;do{result=flock(fd,operation);}while(result&&errno==EINTR);return result;}
/* Reached CreateMutexA/CloseHandle contract for Tribes2's instance detector.
   Ownership is recorded only for a newly created object. Wait/Release and
   duplication remain gates; this is not a general interprocess mutex backend.
   https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexa */
uint32_t tribes_create_mutex(uint64_t base,uint32_t attributes,uint32_t initial,uint32_t name){
    static const char expected[]="Tribes2Game";char value[sizeof expected]={0};
    if(!name)return create_local_mutex(attributes,initial);
    if(name>UINT32_MAX-sizeof expected+1||base>UINTPTR_MAX-name-sizeof expected+1){tribes_set_last_error(87);return 0;}
    for(size_t i=0;i<sizeof value;i++){value[i]=*(const char *)(uintptr_t)(base+name+i);if(!value[i])break;}
    if(attributes||!initial||memcmp(value,expected,sizeof expected)){
        fputs("TribesPad: mutex creation outside the named instance contract is not implemented\n",stderr);abort();
    }
    Handle *h=calloc(1,sizeof *h);if(!h){tribes_set_last_error(8);return 0;}
    pthread_mutex_lock(&registry);
    if(!mutex_directory[0]){pthread_mutex_unlock(&registry);free(h);fputs("TribesPad: named mutex namespace is not configured\n",stderr);abort();}
    char gate_path[PATH_MAX],live_path[PATH_MAX];int gate=-1,live=-1;uint32_t error=5;
    if(snprintf(gate_path,sizeof gate_path,"%s/.tribespad-Tribes2Game.gate",mutex_directory)>=(int)sizeof gate_path||snprintf(live_path,sizeof live_path,"%s/.tribespad-Tribes2Game.live",mutex_directory)>=(int)sizeof live_path){error=206;goto failed;}
    if(next_handle>=0xe2000000){error=8;goto failed;}
    gate=open(gate_path,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(gate<0||lock_file(gate,LOCK_EX))goto failed;
    live=open(live_path,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(live<0)goto failed;
    struct stat st;if(fstat(gate,&st)||!S_ISREG(st.st_mode)||fstat(live,&st)||!S_ISREG(st.st_mode))goto failed;
    int exists=0;
    if(lock_file(live,LOCK_EX|LOCK_NB)){if(errno!=EWOULDBLOCK)goto failed;exists=1;}
    /* Serialize exclusive probe -> shared lifetime conversion with every
       creator; a new opener keeps the object alive independently of its owner. */
    if(lock_file(live,LOCK_SH))goto failed;
    close(gate);h->fd=live;h->instance_mutex=1;if(!exists)h->initial_owner=pthread_self();
    h->value=next_handle++;h->next=handles;handles=h;uint32_t result=h->value;
    pthread_mutex_unlock(&registry);tribes_set_last_error(exists?183:0);return result;
failed:
    if(live>=0)close(live);if(gate>=0)close(gate);pthread_mutex_unlock(&registry);free(h);tribes_set_last_error(error);return 0;
}
static int duplicate(int fd) {
    int copy;do{copy=fcntl(fd,F_DUPFD_CLOEXEC,3);}while(copy<0&&errno==EINTR);
    if(copy>=0 && fcntl(copy,F_SETFD,FD_CLOEXEC)<0){int error=errno;close(copy);errno=error;return -1;}
    return copy;
}
uint32_t tribes_fd_adopt(int fd) {
    if(fd<0||fcntl(fd,F_GETFD)<0){tribes_set_last_error(6);return 0;}
    Handle *h=malloc(sizeof(*h));if(!h){tribes_set_last_error(8);return 0;}
    pthread_mutex_lock(&registry);
    if(next_handle>=0xe2000000){pthread_mutex_unlock(&registry);free(h);tribes_set_last_error(8);return 0;}
    h->value=next_handle++;h->fd=fd;h->instance_mutex=0;h->file=NULL;h->mutex=NULL;h->event=NULL;h->semaphore=NULL;h->completion=NULL;h->thread=NULL;h->next=handles;handles=h;
    uint32_t value=h->value;pthread_mutex_unlock(&registry);return value;
}
static uint32_t adopt_file(int fd,uint32_t access,uint32_t share,const TribesFileOpenInfo *info,int truncate){
    struct stat st;if(fd<0||fstat(fd,&st)||(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)&&!S_ISLNK(st.st_mode))){tribes_set_last_error(5);return 0;}
    Handle *h=calloc(1,sizeof *h);FileShare *file=calloc(1,sizeof *file);if(!h||!file){free(h);free(file);tribes_set_last_error(8);return 0;}
    if(info)file->info=*info;
    pthread_mutex_lock(&registry);
    for(Handle *p=handles;p;p=p->next)if(p->file&&p->file->device==st.st_dev&&p->file->inode==st.st_ino&&((access&7&~p->file->share)||(p->file->access&7&~share))){pthread_mutex_unlock(&registry);free(h);free(file);tribes_set_last_error(32);return 0;}
    if(next_handle>=0xe2000000){pthread_mutex_unlock(&registry);free(h);free(file);tribes_set_last_error(8);return 0;}
    /* Never truncate until every existing share grant and handle allocation
       has succeeded. Hold the registry through truncation/publication. */
    if(truncate&&(!S_ISREG(st.st_mode)||!(access&TRIBES_FILE_WRITE)||ftruncate(fd,0))){
        pthread_mutex_unlock(&registry);free(h);free(file);tribes_set_last_error(5);return 0;
    }
    file->device=st.st_dev;file->inode=st.st_ino;file->access=access;file->share=share;file->refs=1;h->file=file;h->fd=fd;h->value=next_handle++;h->next=handles;handles=h;uint32_t value=h->value;pthread_mutex_unlock(&registry);return value;
}
uint32_t tribes_named_file_fd_adopt(int fd,uint32_t access,uint32_t share,const TribesFileOpenInfo *info){return adopt_file(fd,access,share,info,0);}
uint32_t tribes_named_file_fd_adopt_truncated(int fd,uint32_t access,uint32_t share,const TribesFileOpenInfo *info){return adopt_file(fd,access,share,info,1);}
uint32_t tribes_file_fd_adopt(int fd,uint32_t access,uint32_t share){return tribes_named_file_fd_adopt(fd,access,share,NULL);}
int tribes_fd_borrow_access(uint32_t handle,uint32_t rights) {
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;
    int found=h&&!h->mutex&&!h->event&&!h->semaphore&&!h->completion&&!h->thread&&!h->instance_mutex;
    if(found&&rights){uint32_t allowed;if(h->file)allowed=h->file->access;else{int mode=fcntl(h->fd,F_GETFL)&O_ACCMODE;allowed=mode==O_RDWR?3:mode==O_WRONLY?2:1;}if(rights==TRIBES_FILE_SEEK?!(allowed&3):(rights&~allowed)){pthread_mutex_unlock(&registry);tribes_set_last_error(5);return -1;}}
    int copy=found?duplicate(h->fd):-1;pthread_mutex_unlock(&registry);
    if(copy<0)tribes_set_last_error(found?8:6);return copy;
}
int tribes_fd_borrow(uint32_t handle){return tribes_fd_borrow_access(handle,0);}
static int borrow_nt(uint32_t handle,uint32_t *status,TribesFileOpenInfo *info) {
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;
    int copy=-1;
    if(handle==UINT32_MAX||handle==UINT32_MAX-1||(h&&(h->mutex||h->event||h->semaphore||h->completion||h->thread||h->instance_mutex)))
        *status=0xc0000024u; /* STATUS_OBJECT_TYPE_MISMATCH */
    else if(!h)*status=0xc0000008u; /* STATUS_INVALID_HANDLE */
    else if(info&&(!h->file||!h->file->info.name[0]))*status=0xc00000bbu;
    else {copy=duplicate(h->fd);*status=copy<0?0xc000009au:0;} /* insufficient resources */
    if(copy>=0&&info)*info=h->file->info;
    pthread_mutex_unlock(&registry);return copy;
}
int tribes_fd_borrow_nt(uint32_t handle,uint32_t *status){return borrow_nt(handle,status,NULL);}
int tribes_named_file_borrow_nt(uint32_t handle,uint32_t *status,TribesFileOpenInfo *info){return borrow_nt(handle,status,info);}
uint32_t tribes_close_handle(uint32_t handle) {
    /* Non-debugger pseudo handles are borrowed constants, never registry entries. */
    if(handle==UINT32_MAX||handle==UINT32_MAX-1)return 1;
    pthread_mutex_lock(&registry);Handle **link=&handles;
    while(*link&&(*link)->value!=handle)link=&(*link)->next;
    Handle *h=*link;
    if(!h){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return 0;}
    *link=h->next;int error=0;if(h->mutex){LocalMutex *m=h->mutex;if(!--m->handles)pthread_cond_broadcast(&m->changed);drop_mutex(m);}else if(h->event){Event *e=h->event;if(!--e->handles)pthread_cond_broadcast(&e->changed);drop_event(e);}else if(h->thread){drop_thread(h->thread);}else if(h->completion){CompletionPort *p=h->completion;p->open=0;pthread_cond_broadcast(&p->changed);drop_completion(p);}else if(h->semaphore){Semaphore *s=h->semaphore;if(!--s->handles)pthread_cond_broadcast(&s->changed);drop_semaphore(s);}else{error=close(h->fd);if(h->file&&!--h->file->refs)free(h->file);}free(h);pthread_cond_broadcast(&multi_changed);pthread_mutex_unlock(&registry);
    if(error){tribes_set_last_error(6);return 0;}return 1;
}
static int std_index(uint32_t selector) {
    if(selector==UINT32_C(0xfffffff6))return 0;
    if(selector==UINT32_C(0xfffffff5))return 1;
    if(selector==UINT32_C(0xfffffff4))return 2;
    return -1;
}
static void initialize_standard(void) {
    uint32_t saved=tribes_get_last_error();
    for(int i=0;i<3;i++) {
        int copy=duplicate(i);if(copy<0)continue;
        standard[i]=tribes_fd_adopt(copy);if(!standard[i])close(copy);
    }
    tribes_set_last_error(saved);
}
uint32_t tribes_get_std_handle(uint32_t selector) {
    int i=std_index(selector);if(i<0){tribes_set_last_error(6);return UINT32_MAX;}
    pthread_once(&startup,initialize_standard);pthread_mutex_lock(&registry);
    uint32_t result=standard[i];pthread_mutex_unlock(&registry);return result;
}
uint32_t tribes_set_std_handle(uint32_t selector,uint32_t handle) {
    int i=std_index(selector);if(i<0){tribes_set_last_error(6);return 0;}
    pthread_once(&startup,initialize_standard);pthread_mutex_lock(&registry);
    standard[i]=handle;pthread_mutex_unlock(&registry);return 1;
}
uint32_t tribes_get_file_type(uint32_t handle) {
    int fd=tribes_fd_borrow(handle);if(fd<0)return 0;
    struct stat info;int error=fstat(fd,&info);close(fd);
    if(error){tribes_set_last_error(6);return 0;}
    if(S_ISCHR(info.st_mode))return 2;
    if(S_ISFIFO(info.st_mode)||S_ISSOCK(info.st_mode))return 3;
    if(S_ISREG(info.st_mode)||S_ISDIR(info.st_mode)||S_ISLNK(info.st_mode)||S_ISBLK(info.st_mode))return 1;
    tribes_set_last_error(0);return 0;
}

/* Win32 current-process pseudo handle; never an owned descriptor. */
uint32_t tribes_get_current_process(void) { return UINT32_MAX; }
/* Resolves to the caller when consumed; not an owned/thread ID handle. */
uint32_t tribes_get_current_thread(void) { return UINT32_MAX-1; }
/* Same-process descriptor duplication. Other object kinds and reduced-access
   duplicates remain explicit gates; source-close and inheritance are real. */
uint32_t tribes_duplicate_handle(uint64_t base,uint32_t source_process,uint32_t source,
    uint32_t target_process,uint32_t output,uint32_t access,uint32_t inherit,uint32_t options) {
    (void)access;
    if(options&~3u || (!(options&2u)&&target_process)) {
        fprintf(stderr,"TribesPad: unsupported DuplicateHandle access/options\n");abort();
    }
    if(source_process!=UINT32_MAX || (target_process!=UINT32_MAX&&target_process!=0)) {
        if(options&1u)(void)tribes_close_handle(source);
        tribes_set_last_error(6);return 0;
    }
    if(!target_process) {
        if(!(options&1u)){tribes_set_last_error(87);return 0;}
        return tribes_close_handle(source);
    }
    pthread_mutex_lock(&registry);Handle *original=handles;while(original&&original->value!=source)original=original->next;
    TribesThreadObject *thread=source==UINT32_MAX-1?current_thread_object:original?original->thread:NULL;
    if(thread){
        Handle *copy=next_handle<0xe2000000?calloc(1,sizeof *copy):NULL;
        if(!copy||thread->refs==UINT32_MAX){free(copy);pthread_mutex_unlock(&registry);if(options&1u)(void)tribes_close_handle(source);tribes_set_last_error(8);return 0;}
        copy->value=next_handle++;copy->fd=-1;copy->thread=thread;++thread->refs;copy->next=handles;handles=copy;uint32_t result=copy->value;pthread_mutex_unlock(&registry);
        if(options&1u)(void)tribes_close_handle(source);
        if(output){if(output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){(void)tribes_close_handle(result);tribes_set_last_error(87);return 0;}memcpy((void *)(uintptr_t)(base+output),&result,4);}return 1;
    }
    if(original&&original->mutex){
        LocalMutex *m=original->mutex;Handle *copy=next_handle<0xe2000000&&m->refs<UINT32_MAX?calloc(1,sizeof *copy):NULL;
        if(!copy){pthread_mutex_unlock(&registry);if(options&1u)(void)tribes_close_handle(source);tribes_set_last_error(8);return 0;}
        copy->value=next_handle++;copy->fd=-1;copy->mutex=m;++m->refs;++m->handles;copy->next=handles;handles=copy;uint32_t result=copy->value;pthread_mutex_unlock(&registry);
        if(options&1u)(void)tribes_close_handle(source);
        if(output){if(output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){(void)tribes_close_handle(result);tribes_set_last_error(87);return 0;}memcpy((void *)(uintptr_t)(base+output),&result,4);}return 1;
    }
    if(original&&original->event){
        Event *e=original->event;Handle *copy=next_handle<0xe2000000&&e->refs<UINT32_MAX?calloc(1,sizeof *copy):NULL;
        if(!copy){pthread_mutex_unlock(&registry);if(options&1u)(void)tribes_close_handle(source);tribes_set_last_error(8);return 0;}
        copy->value=next_handle++;copy->fd=-1;copy->event=e;++e->refs;++e->handles;copy->next=handles;handles=copy;uint32_t result=copy->value;pthread_mutex_unlock(&registry);
        if(options&1u)(void)tribes_close_handle(source);
        if(output){if(output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){(void)tribes_close_handle(result);tribes_set_last_error(87);return 0;}memcpy((void *)(uintptr_t)(base+output),&result,4);}return 1;
    }
    if(original&&original->completion){pthread_mutex_unlock(&registry);fprintf(stderr,"TribesPad: completion port duplication is not implemented\n");abort();}
    if(original&&original->instance_mutex){pthread_mutex_unlock(&registry);fputs("TribesPad: named mutex duplication is not implemented\n",stderr);abort();}
    if(original&&original->file){
        int fd=duplicate(original->fd);Handle *copy=fd>=0&&next_handle<0xe2000000?calloc(1,sizeof *copy):NULL;
        if(!copy){if(fd>=0)close(fd);pthread_mutex_unlock(&registry);if(options&1u)(void)tribes_close_handle(source);tribes_set_last_error(8);return 0;}
        if(fcntl(fd,F_SETFD,inherit?0:FD_CLOEXEC)<0){close(fd);free(copy);pthread_mutex_unlock(&registry);if(options&1u)(void)tribes_close_handle(source);tribes_set_last_error(6);return 0;}
        copy->fd=fd;copy->file=original->file;copy->file->refs++;copy->value=next_handle++;copy->next=handles;handles=copy;uint32_t result=copy->value;pthread_mutex_unlock(&registry);
        if(options&1u)(void)tribes_close_handle(source);
        if(output){if(output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){(void)tribes_close_handle(result);tribes_set_last_error(87);return 0;}memcpy((void *)(uintptr_t)(base+output),&result,4);}return 1;
    }
    if(original&&original->semaphore){
        Handle *copy=next_handle<0xe2000000?calloc(1,sizeof *copy):NULL;
        if(!copy){pthread_mutex_unlock(&registry);if(options&1u)(void)tribes_close_handle(source);tribes_set_last_error(8);return 0;}
        copy->value=next_handle++;copy->fd=-1;copy->semaphore=original->semaphore;copy->semaphore->refs++;copy->semaphore->handles++;copy->next=handles;handles=copy;uint32_t result=copy->value;pthread_mutex_unlock(&registry);
        if(options&1u)(void)tribes_close_handle(source);
        if(output){if(output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){(void)tribes_close_handle(result);tribes_set_last_error(87);return 0;}memcpy((void *)(uintptr_t)(base+output),&result,4);}return 1;
    }
    pthread_mutex_unlock(&registry);
    int fd=tribes_fd_borrow(source);uint32_t saved=tribes_get_last_error();
    if(options&1u)(void)tribes_close_handle(source);
    if(fd<0){tribes_set_last_error(saved);return 0;}
    if(fcntl(fd,F_SETFD,inherit?0:FD_CLOEXEC)<0){close(fd);tribes_set_last_error(6);return 0;}
    uint32_t result=tribes_fd_adopt(fd);if(!result){close(fd);return 0;}
    if(output) {
        if(output>UINT32_MAX-3 || base>UINTPTR_MAX-output-3) {
            (void)tribes_close_handle(result);tribes_set_last_error(87);return 0;
        }
        memcpy((void *)(uintptr_t)(base+output),&result,4);
    }
    return 1;
}

/* Unnamed process-local events share the existing handle/wait registry.
   Selected waiters retain their wake even if ResetEvent runs before they resume.
   Named objects/security attributes remain explicit boundaries. */
uint32_t tribes_create_event(uint64_t base,uint32_t attributes,uint32_t manual,uint32_t initial,uint32_t name){
    (void)base;if(attributes||name){fputs("TribesPad: named/security event creation is not implemented\n",stderr);abort();}
    Event *e=calloc(1,sizeof *e);Handle *h=calloc(1,sizeof *h);
    if(!e||!h){free(e);free(h);tribes_set_last_error(8);return 0;}
    if(pthread_cond_init(&e->changed,NULL)){free(e);free(h);tribes_set_last_error(8);return 0;}
    e->refs=e->handles=1;e->manual=!!manual;e->signaled=!!initial;h->fd=-1;h->event=e;
    pthread_mutex_lock(&registry);if(next_handle>=0xe2000000){pthread_mutex_unlock(&registry);drop_event(e);free(h);tribes_set_last_error(8);return 0;}
    h->value=next_handle++;h->next=handles;handles=h;uint32_t result=h->value;pthread_mutex_unlock(&registry);return result;
}
uint32_t tribes_set_event(uint32_t handle){
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;Event *e=h?h->event:NULL;
    if(!e){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return 0;}
    if(!e->signaled){e->signaled=1;for(EventWaiter *w=e->waiters;w;w=w->next)if(!w->ready){w->ready=1;if(!e->manual){e->signaled=0;break;}}pthread_cond_broadcast(&e->changed);pthread_cond_broadcast(&multi_changed);}
    pthread_mutex_unlock(&registry);return 1;
}
uint32_t tribes_reset_event(uint32_t handle){
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;Event *e=h?h->event:NULL;
    if(!e){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return 0;}e->signaled=0;pthread_mutex_unlock(&registry);return 1;
}

/* Unnamed, process-local counting semaphores. Named/security descriptors remain
   strict gates. Win32 LONG counts are signed; -1 is not an unlimited maximum. */
uint32_t tribes_create_semaphore(uint64_t base,uint32_t attributes,uint32_t initial,uint32_t maximum,uint32_t name){
    (void)base;if(attributes||name){fprintf(stderr,"TribesPad: named/security semaphore creation is not implemented\n");abort();}
    if((int32_t)initial<0||(int32_t)maximum<=0||initial>maximum){tribes_set_last_error(87);return 0;}
    Semaphore *s=calloc(1,sizeof *s);Handle *h=calloc(1,sizeof *h);if(!s||!h){free(s);free(h);tribes_set_last_error(8);return 0;}
    if(pthread_cond_init(&s->changed,NULL)){free(s);free(h);tribes_set_last_error(8);return 0;}
    s->count=initial;s->maximum=maximum;s->refs=s->handles=1;h->fd=-1;h->semaphore=s;
    pthread_mutex_lock(&registry);if(next_handle>=0xe2000000){pthread_mutex_unlock(&registry);drop_semaphore(s);free(h);tribes_set_last_error(8);return 0;}h->value=next_handle++;h->next=handles;handles=h;uint32_t value=h->value;pthread_mutex_unlock(&registry);return value;
}
uint32_t tribes_release_semaphore(uint64_t base,uint32_t handle,uint32_t count,uint32_t previous){
    if((int32_t)count<=0||(previous&&(previous>UINT32_MAX-3||base>UINTPTR_MAX-previous-3))){tribes_set_last_error(87);return 0;}
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;Semaphore *s=h?h->semaphore:NULL;
    if(!s){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return 0;}
    if(count>s->maximum-s->count){pthread_mutex_unlock(&registry);tribes_set_last_error(298);return 0;}
    uint32_t old=s->count;s->count+=count;if(previous)memcpy((void *)(uintptr_t)(base+previous),&old,4);pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&registry);return 1;
}
static int wait_changed(pthread_cond_t *changed,uint32_t ms,const struct timespec *deadline) {
    if(!ms)return ETIMEDOUT;
    if(ms==UINT32_MAX)return pthread_cond_wait(changed,&registry);
    struct timespec now,left;if(clock_gettime(CLOCK_MONOTONIC,&now))return EINVAL;
    left.tv_sec=deadline->tv_sec-now.tv_sec;left.tv_nsec=deadline->tv_nsec-now.tv_nsec;
    if(left.tv_nsec<0){--left.tv_sec;left.tv_nsec+=1000000000;}
    if(left.tv_sec<0||(!left.tv_sec&&!left.tv_nsec))return ETIMEDOUT;
    return pthread_cond_timedwait_relative_np(changed,&registry,&left);
}
/* Reached Miles wait-any pair: shutdown manual event at index 0 and its
   local recursive mutex at index 1. Both are acquired/tested under the same
   registry lock. General wait-all and other object combinations remain gates. */
uint32_t tribes_wait_multiple_objects(uint64_t base,uint32_t count,uint32_t addresses,uint32_t all,uint32_t milliseconds){
    if(count!=2||all){fputs("TribesPad: unsupported multiple-wait count/mode\n",stderr);abort();}
    if(!addresses||addresses>UINT32_MAX-7||base>UINTPTR_MAX-addresses-7){tribes_set_last_error(87);return UINT32_MAX;}
    uint32_t values[2];memcpy(values,(void *)(uintptr_t)(base+addresses),8);
    struct timespec deadline={0,0};if(milliseconds&&milliseconds!=UINT32_MAX){if(clock_gettime(CLOCK_MONOTONIC,&deadline)){tribes_set_last_error(8);return UINT32_MAX;}deadline.tv_sec+=milliseconds/1000;deadline.tv_nsec+=(long)(milliseconds%1000)*1000000;if(deadline.tv_nsec>=1000000000){deadline.tv_sec++;deadline.tv_nsec-=1000000000;}}
    pthread_mutex_lock(&registry);Handle *a=handles,*b=handles;while(a&&a->value!=values[0])a=a->next;while(b&&b->value!=values[1])b=b->next;
    if(!a||!b||values[0]==values[1]){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return UINT32_MAX;}
    Event *e=a->event;LocalMutex *m=b->mutex;
    if(!e||!e->manual||!m){pthread_mutex_unlock(&registry);fputs("TribesPad: unsupported multiple-wait object pair\n",stderr);abort();}
    if(e->refs==UINT32_MAX||m->refs==UINT32_MAX||!register_mutex_owner()){pthread_mutex_unlock(&registry);tribes_set_last_error(8);return UINT32_MAX;}
    ++e->refs;++m->refs;EventWaiter waiter={0,e->waiters};e->waiters=&waiter;uint32_t result=UINT32_MAX;
    for(;;){
        if(!e->handles||!m->handles){tribes_set_last_error(6);break;}
        if(e->signaled||waiter.ready){result=0;break;}
        if(!m->depth||pthread_equal(m->owner,pthread_self())){
            if(m->depth==UINT32_MAX){tribes_set_last_error(8);break;}m->owner=pthread_self();++m->depth;result=m->abandoned?129:1;m->abandoned=0;break;
        }
#ifdef TRIBESPAD_EVENT_OBSERVER
        extern void tribes_event_wait_observation(void);tribes_event_wait_observation();
#endif
        int error=wait_changed(&multi_changed,milliseconds,&deadline);if(error){if(error==ETIMEDOUT)result=258;else tribes_set_last_error(8);break;}
    }
    EventWaiter **p=&e->waiters;while(*p!=&waiter)p=&(*p)->next;*p=waiter.next;drop_event(e);drop_mutex(m);pthread_mutex_unlock(&registry);return result;
}
uint32_t tribes_wait_single_object(uint32_t handle,uint32_t milliseconds){
    struct timespec deadline={0,0};if(milliseconds!=UINT32_MAX&&milliseconds){if(clock_gettime(CLOCK_MONOTONIC,&deadline)){tribes_set_last_error(8);return UINT32_MAX;}deadline.tv_sec+=milliseconds/1000;deadline.tv_nsec+=(long)(milliseconds%1000)*1000000;if(deadline.tv_nsec>=1000000000){deadline.tv_sec++;deadline.tv_nsec-=1000000000;}}
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;
    TribesThreadObject *thread=handle==UINT32_MAX-1?current_thread_object:h?h->thread:NULL;
    if(thread){
        if(thread->refs==UINT32_MAX){pthread_mutex_unlock(&registry);tribes_set_last_error(8);return UINT32_MAX;}
        ++thread->refs;int error=0;while(!thread->finished&&!(error=wait_changed(&thread->changed,milliseconds,&deadline))){}
        uint32_t result=thread->finished?0:error==ETIMEDOUT?258:UINT32_MAX;
        drop_thread(thread);pthread_mutex_unlock(&registry);if(result==UINT32_MAX)tribes_set_last_error(8);return result;
    }
    if(!h){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return UINT32_MAX;}
    if(h->event){
        Event *e=h->event;if(e->signaled){if(!e->manual)e->signaled=0;pthread_mutex_unlock(&registry);return 0;}
        if(!milliseconds){pthread_mutex_unlock(&registry);return 258;}
        if(e->refs==UINT32_MAX){pthread_mutex_unlock(&registry);tribes_set_last_error(8);return UINT32_MAX;}
        ++e->refs;EventWaiter waiter={0,e->waiters};e->waiters=&waiter;int error=0;
#ifdef TRIBESPAD_EVENT_OBSERVER
        extern void tribes_event_wait_observation(void);tribes_event_wait_observation();
#endif
        while(!waiter.ready&&e->handles&&!(error=wait_changed(&e->changed,milliseconds,&deadline))){}
        uint32_t result=waiter.ready?0:!e->handles?UINT32_MAX:error==ETIMEDOUT?258:UINT32_MAX;
        if(result==UINT32_MAX)tribes_set_last_error(e->handles?8:6);
        EventWaiter **p=&e->waiters;while(*p!=&waiter)p=&(*p)->next;*p=waiter.next;drop_event(e);pthread_mutex_unlock(&registry);return result;
    }
    if(h->mutex){
        LocalMutex *m=h->mutex;
        if(m->refs==UINT32_MAX||!register_mutex_owner()){pthread_mutex_unlock(&registry);tribes_set_last_error(8);return UINT32_MAX;}
        ++m->refs;int error=0;
        while(m->handles&&m->depth&&!pthread_equal(m->owner,pthread_self())&&!(error=wait_changed(&m->changed,milliseconds,&deadline))){}
        uint32_t result;
        if(!m->handles){result=UINT32_MAX;tribes_set_last_error(6);}
        else if(!m->depth||pthread_equal(m->owner,pthread_self())){
            if(m->depth==UINT32_MAX){result=UINT32_MAX;tribes_set_last_error(8);}
            else{result=m->abandoned?128:0;m->abandoned=0;m->owner=pthread_self();++m->depth;}
        }else{result=error==ETIMEDOUT?258:UINT32_MAX;if(result==UINT32_MAX)tribes_set_last_error(8);}
        drop_mutex(m);pthread_mutex_unlock(&registry);return result;
    }
    if(!h->semaphore){pthread_mutex_unlock(&registry);fprintf(stderr,"TribesPad: waiting on a non-semaphore object is not implemented\n");abort();}
    Semaphore *s=h->semaphore;s->refs++;uint32_t result=258;int error=0;
    while(!s->count&&s->handles){
        if(!milliseconds)break;
        if(milliseconds==UINT32_MAX)error=pthread_cond_wait(&s->changed,&registry);
        else{struct timespec now,remaining;if(clock_gettime(CLOCK_MONOTONIC,&now)){error=EINVAL;break;}remaining.tv_sec=deadline.tv_sec-now.tv_sec;remaining.tv_nsec=deadline.tv_nsec-now.tv_nsec;if(remaining.tv_nsec<0){remaining.tv_sec--;remaining.tv_nsec+=1000000000;}if(remaining.tv_sec<0||(!remaining.tv_sec&&!remaining.tv_nsec))break;
            error=pthread_cond_timedwait_relative_np(&s->changed,&registry,&remaining);
        }
        if(error==ETIMEDOUT){error=0;break;}if(error)break;
    }
    if(!s->handles||error){result=UINT32_MAX;tribes_set_last_error(!s->handles?6:8);}else if(s->count){s->count--;result=0;}
    drop_semaphore(s);pthread_mutex_unlock(&registry);return result;
}

/* The Winsock endpoint provider is optional for standalone queue fixtures.
   The full engine links its strong implementation and real descriptor registry. */
__attribute__((weak,noinline)) uint32_t tribes_socket_associate_completion(uint64_t base,uint32_t socket,uint32_t port,uint32_t key){
    (void)base;(void)socket;(void)port;(void)key;tribes_set_last_error(50);return 0;
}
/* Completion queues for the observed libuv single-consumer loop. Multi-consumer
   scheduling and regular-file associations remain explicit gates.
   https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports */
uint32_t tribes_create_io_completion_port(uint64_t base,uint32_t file,uint32_t existing,uint32_t key,uint32_t concurrency){
    if(file>=0xb1000001u&&file<0xb2000000u&&existing){
        /* Concurrency is ignored when attaching to an existing port. */
        return tribes_socket_associate_completion(base,file,existing,key);
    }
    (void)key;
    if(file!=UINT32_MAX||existing||concurrency!=1){fprintf(stderr,"TribesPad: completion port association/default or multi-thread scheduling is not implemented\n");abort();}
    CompletionPort *p=calloc(1,sizeof *p);Handle *h=calloc(1,sizeof *h);
    if(!p||!h){free(p);free(h);tribes_set_last_error(8);return 0;}
    if(pthread_cond_init(&p->changed,NULL)){free(p);free(h);tribes_set_last_error(8);return 0;}
    p->refs=1;p->open=1;h->fd=-1;h->completion=p;
    pthread_mutex_lock(&registry);
    if(next_handle>=0xe2000000){pthread_mutex_unlock(&registry);drop_completion(p);free(h);tribes_set_last_error(8);return 0;}
    h->value=next_handle++;h->next=handles;handles=h;uint32_t value=h->value;
    pthread_mutex_unlock(&registry);return value;
}
TribesCompletionBinding *tribes_completion_binding_create(uint32_t handle,uint32_t key){
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;
    CompletionPort *p=h?h->completion:NULL;
    if(!p||!p->open){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return NULL;}
    TribesCompletionBinding *b=malloc(sizeof *b);
    if(!b||p->refs==UINT32_MAX){free(b);pthread_mutex_unlock(&registry);tribes_set_last_error(8);return NULL;}
    p->refs++;b->port=p;b->key=key;pthread_mutex_unlock(&registry);return b;
}
void tribes_completion_binding_release(TribesCompletionBinding *b){
    if(!b)return;pthread_mutex_lock(&registry);drop_completion(b->port);free(b);pthread_mutex_unlock(&registry);
}
static uint32_t post_completion_locked(CompletionPort *p,uint32_t bytes,uint32_t key,uint32_t overlapped,uint64_t base,uint32_t status,uint32_t error){
    if(!p->open){tribes_set_last_error(735);return 0;}
    CompletionPacket *packet=malloc(sizeof *packet);
    if(!packet){tribes_set_last_error(8);return 0;}
    packet->bytes=bytes;packet->key=key;packet->overlapped=overlapped;packet->base=base;packet->status=status;packet->error=error;packet->next=NULL;
    if(p->tail)p->tail->next=packet;else p->head=packet;p->tail=packet;
    pthread_cond_broadcast(&p->changed);return 1;
}
uint32_t tribes_completion_binding_post(TribesCompletionBinding *b,uint32_t bytes,uint32_t overlapped){
    if(!b){tribes_set_last_error(6);return 0;}
    pthread_mutex_lock(&registry);uint32_t result=post_completion_locked(b->port,bytes,b->key,overlapped,0,0,0);
    pthread_mutex_unlock(&registry);return result;
}
uint32_t tribes_completion_binding_post_io(TribesCompletionBinding *b,uint64_t base,uint32_t bytes,uint32_t overlapped,uint32_t status,uint32_t error){
    if(!b||!base||!overlapped||overlapped>UINT32_MAX-19||base>UINTPTR_MAX-overlapped-19){tribes_set_last_error(87);return 0;}
    pthread_mutex_lock(&registry);uint32_t result=post_completion_locked(b->port,bytes,b->key,overlapped,base,status,error);
    pthread_mutex_unlock(&registry);return result;
}
uint32_t tribes_post_queued_completion(uint32_t handle,uint32_t bytes,uint32_t key,uint32_t overlapped){
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;
    CompletionPort *p=h?h->completion:NULL;
    if(!p){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return 0;}
    uint32_t result=post_completion_locked(p,bytes,key,overlapped,0,0,0);
    pthread_mutex_unlock(&registry);return result;
}
static _Thread_local uint64_t completion_thread;
static uint64_t next_completion_thread=1;
/* Detach at least one available packet, then as many immediately available
   packets as the caller can receive. Both public dequeue APIs share ownership,
   timeout, close and single-consumer behavior. Never wait to fill the batch. */
static CompletionPacket *take_completion_packets(uint32_t handle,uint32_t milliseconds,uint32_t capacity,uint32_t *count){
    *count=0;
    struct timespec deadline={0,0};
    if(milliseconds&&milliseconds!=UINT32_MAX){
        if(clock_gettime(CLOCK_MONOTONIC,&deadline)){tribes_set_last_error(8);return NULL;}
        deadline.tv_sec+=milliseconds/1000;deadline.tv_nsec+=(long)(milliseconds%1000)*1000000;
        if(deadline.tv_nsec>=1000000000){deadline.tv_sec++;deadline.tv_nsec-=1000000000;}
    }
    pthread_mutex_lock(&registry);Handle *h=handles;while(h&&h->value!=handle)h=h->next;
    CompletionPort *p=h?h->completion:NULL;
    if(!p){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return NULL;}
    if(!completion_thread){if(!next_completion_thread)abort();completion_thread=next_completion_thread++;}
    if(p->consumer&&p->consumer!=completion_thread){pthread_mutex_unlock(&registry);fprintf(stderr,"TribesPad: completion port consumer handoff/multi-consumer scheduling is not implemented\n");abort();}
    p->consumer=completion_thread;p->refs++;int error=0;
    while(!p->head&&p->open){
        if(!milliseconds)break;
#ifdef TRIBESPAD_COMPLETION_WAIT_OBSERVER
        extern void tribes_completion_wait_observer(uint32_t);
        tribes_completion_wait_observer(handle);
#endif
        if(milliseconds==UINT32_MAX)error=pthread_cond_wait(&p->changed,&registry);
        else{
            struct timespec now,remaining;if(clock_gettime(CLOCK_MONOTONIC,&now)){error=EINVAL;break;}
            remaining.tv_sec=deadline.tv_sec-now.tv_sec;remaining.tv_nsec=deadline.tv_nsec-now.tv_nsec;
            if(remaining.tv_nsec<0){remaining.tv_sec--;remaining.tv_nsec+=1000000000;}
            if(remaining.tv_sec<0||(!remaining.tv_sec&&!remaining.tv_nsec))break;
            error=pthread_cond_timedwait_relative_np(&p->changed,&registry,&remaining);
        }
        if(error==ETIMEDOUT){error=0;break;}if(error)break;
    }
    CompletionPacket *first=NULL;
    if(!p->open)tribes_set_last_error(735);
    else if(error)tribes_set_last_error(8);
    else if(!p->head)tribes_set_last_error(258);
    else{
        first=p->head;CompletionPacket *last=first;*count=1;
        while(*count<capacity&&last->next){last=last->next;(*count)++;}
        p->head=last->next;last->next=NULL;if(!p->head)p->tail=NULL;
    }
    drop_completion(p);pthread_mutex_unlock(&registry);return first;
}
uint32_t tribes_get_queued_completion(uint64_t base,uint32_t handle,uint32_t bytes,uint32_t key,uint32_t overlapped,uint32_t milliseconds){
    uint32_t outputs[]={bytes,key,overlapped};
    for(unsigned i=0;i<3;i++)if(!outputs[i]||outputs[i]>UINT32_MAX-3||base>UINTPTR_MAX-outputs[i]-3){tribes_set_last_error(87);return 0;}
    uint32_t zero=0,count;memcpy((void *)(uintptr_t)(base+overlapped),&zero,4);
    CompletionPacket *packet=take_completion_packets(handle,milliseconds,1,&count);
    if(!packet)return 0;
    memcpy((void *)(uintptr_t)(base+bytes),&packet->bytes,4);
    memcpy((void *)(uintptr_t)(base+key),&packet->key,4);
    memcpy((void *)(uintptr_t)(base+overlapped),&packet->overlapped,4);
    if(packet->base){uint32_t block[]={packet->status,packet->bytes};memcpy((void *)(uintptr_t)(packet->base+packet->overlapped),block,8);}
    uint32_t error=packet->error;free(packet);if(error){tribes_set_last_error(error);return 0;}return 1;
}
/* i686 OVERLAPPED_ENTRY is four DWORDs: key, pointer, Internal, bytes.
   Public posted pointers are opaque. Native I/O carries its real status and
   writes the checked status block when its packet is removed.
   https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatusex
   https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped_entry */
uint32_t tribes_get_queued_completion_ex(uint64_t base,uint32_t handle,uint32_t entries,uint32_t capacity,uint32_t removed,uint32_t milliseconds,uint32_t alertable){
    uint64_t end=(uint64_t)entries+(uint64_t)capacity*16;
    if(!entries||!capacity||end>UINT64_C(0x100000000)||base>UINTPTR_MAX-end||!removed||removed>UINT32_MAX-3||base>UINTPTR_MAX-removed-3){tribes_set_last_error(87);return 0;}
    if(alertable){fputs("TribesPad: alertable completion dequeue/APC delivery is not implemented\n",stderr);abort();}
    uint32_t count=0;memcpy((void *)(uintptr_t)(base+removed),&count,4);
    CompletionPacket *packet=take_completion_packets(handle,milliseconds,capacity,&count);
    if(!packet)return 0;
    uint32_t index=0;
    while(packet){
        CompletionPacket *next=packet->next;uint32_t row[]={packet->key,packet->overlapped,packet->status,packet->bytes};
        if(packet->base){uint32_t block[]={packet->status,packet->bytes};memcpy((void *)(uintptr_t)(packet->base+packet->overlapped),block,8);}
        memcpy((void *)(uintptr_t)(base+entries+(uint64_t)index*16),row,sizeof row);free(packet);packet=next;index++;
    }
    memcpy((void *)(uintptr_t)(base+removed),&count,4);return 1;
}

/* Thread handles share the kernel-object registry with semaphore/file handles.
   Closing a handle never terminates its thread. The running owner, duplicates
   and pending waits each hold independent references. */
TribesThreadObject *tribes_thread_object_create(uint32_t id,uint32_t suspended,uint32_t *handle){
    if(!id||suspended>1||!handle){tribes_set_last_error(87);return NULL;}
    TribesThreadObject *t=calloc(1,sizeof *t);Handle *h=calloc(1,sizeof *h);
    if(!t||!h){free(t);free(h);tribes_set_last_error(8);return NULL;}
    if(pthread_cond_init(&t->changed,NULL)){free(t);free(h);tribes_set_last_error(8);return NULL;}
    t->refs=2;t->id=id;t->suspended=suspended;t->code=259;h->fd=-1;h->thread=t;
    pthread_mutex_lock(&registry);
    if(next_handle>=0xe2000000){pthread_mutex_unlock(&registry);pthread_cond_destroy(&t->changed);free(t);free(h);tribes_set_last_error(8);return NULL;}
    h->value=next_handle++;h->next=handles;handles=h;++live_threads;*handle=h->value;
    pthread_mutex_unlock(&registry);return t;
}
void tribes_thread_object_set_current(TribesThreadObject *t){
    pthread_mutex_lock(&registry);if(t){if(t->native_ready&&!pthread_equal(t->native,pthread_self()))abort();t->native=pthread_self();t->native_ready=1;pthread_cond_broadcast(&t->changed);}current_thread_object=t;pthread_mutex_unlock(&registry);
}
void tribes_thread_object_wait_start(TribesThreadObject *t){
    pthread_mutex_lock(&registry);while(t->suspended)if(pthread_cond_wait(&t->changed,&registry))abort();
    if(t->started||t->finished)abort();t->started=1;pthread_mutex_unlock(&registry);
}
void tribes_thread_object_finish(TribesThreadObject *t,uint32_t code){
    pthread_mutex_lock(&registry);if(t->finished)abort();if(t==current_thread_object)abandon_mutexes_locked();t->code=code;t->finished=1;
    if(pthread_cond_broadcast(&t->changed))abort();drop_thread(t);pthread_mutex_unlock(&registry);
}
static TribesThreadObject *find_thread(uint32_t handle){
    if(handle==UINT32_MAX-1)return current_thread_object;
    for(Handle *h=handles;h;h=h->next)if(h->value==handle)return h->thread;
    return NULL;
}
/* Explicit Apple SCHED_OTHER policy, not Windows realtime equivalence.
   Ordinary relative levels use its middle priority plus the Windows offset;
   IDLE/TIME_CRITICAL use its queried endpoints. Native failures are retained. */
uint32_t tribes_set_thread_priority(uint32_t handle,uint32_t value){
    int32_t priority=(int32_t)value;if(priority!=-15&&priority!=15&&(priority < -2||priority>2)){tribes_set_last_error(87);return 0;}
    pthread_mutex_lock(&registry);TribesThreadObject *t=find_thread(handle);
    if(!t){pthread_mutex_unlock(&registry);tribes_set_last_error(6);return 0;}
    if(t->refs==UINT32_MAX){pthread_mutex_unlock(&registry);tribes_set_last_error(8);return 0;}++t->refs;
    while(!t->native_ready&&!t->finished)if(pthread_cond_wait(&t->changed,&registry))abort();
    int error=0;int requested=0;
    if(t->finished)error=ESRCH;
    else {int lo=sched_get_priority_min(SCHED_OTHER),hi=sched_get_priority_max(SCHED_OTHER);
        if(lo<0||hi<lo)error=ENOTSUP;
        else {requested=priority==-15?lo:priority==15?hi:lo+(hi-lo)/2+priority;if(requested<lo)requested=lo;if(requested>hi)requested=hi;struct sched_param param={0};param.sched_priority=requested;error=pthread_setschedparam(t->native,SCHED_OTHER,&param);}
    }
    drop_thread(t);pthread_mutex_unlock(&registry);
    if(error){tribes_set_last_error(error==EPERM||error==EACCES?5:error==ESRCH?6:error==EINVAL?87:error==ENOTSUP?50:31);return 0;}
    fprintf(stderr,"TribesPad: thread priority applied handle=0x%x Windows=%d native=%d\n",handle,priority,requested);return 1;
}
uint32_t tribes_resume_thread(uint32_t handle){
    pthread_mutex_lock(&registry);TribesThreadObject *t=find_thread(handle);uint32_t value=UINT32_MAX;
    if(t){value=t->suspended;if(value){--t->suspended;if(pthread_cond_broadcast(&t->changed))abort();}}
    pthread_mutex_unlock(&registry);if(value==UINT32_MAX)tribes_set_last_error(6);return value;
}
uint32_t tribes_get_exit_code_thread(uint64_t base,uint32_t handle,uint32_t output){
    if(!output||output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){tribes_set_last_error(87);return 0;}
    pthread_mutex_lock(&registry);TribesThreadObject *t=find_thread(handle);int found=t!=NULL;
    if(t){uint32_t code=t->code;memcpy((void *)(uintptr_t)(base+output),&code,4);}
    pthread_mutex_unlock(&registry);if(!found)tribes_set_last_error(6);return found;
}
uint32_t tribes_get_thread_id(uint32_t handle){
    pthread_mutex_lock(&registry);TribesThreadObject *t=find_thread(handle);uint32_t id=t?t->id:0;
    pthread_mutex_unlock(&registry);if(!id)tribes_set_last_error(6);return id;
}
uint32_t tribes_thread_object_live_count(void){pthread_mutex_lock(&registry);uint32_t n=live_threads;pthread_mutex_unlock(&registry);return n;}
