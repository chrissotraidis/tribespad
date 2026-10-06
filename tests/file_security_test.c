#include "../port/core/guest_file.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/guest_heap.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
extern void tribes_enter(_cpu *,uint32_t);
extern char *file_security_realpath(const char *,char *);
extern void *file_security_wrong_realpath(void);
static unsigned char *memory;
static uint64_t base;
static struct stat fixture;
static int owner(void){
    int found=-1;
    for(int fd=0;fd<256;fd++){
        struct stat st;
        if(!fstat(fd,&st)&&st.st_dev==fixture.st_dev&&st.st_ino==fixture.st_ino){assert(found<0);found=fd;}
    }
    return found;
}
static void inherited_after_exec(const char *self,int fd,int inherit){
    char descriptor[32];snprintf(descriptor,sizeof descriptor,"%d",fd);
    char *args[]={(char *)self,"--child",descriptor,inherit?"1":"0",NULL};
    pid_t child;assert(!posix_spawn(&child,self,NULL,NULL,args,environ));
    int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
}
static void fault(uint32_t security){
    pid_t child=fork();assert(child>=0);
    if(!child){tribes_create_file_w(base,0x11000,0x80000000,1,security,3,0x80,0);_exit(90);}
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
    assert(owner()==-1);
}
static void external_fault(unsigned kind){
    pid_t child=fork();assert(child>=0);
    if(!child){char host[4096];if(kind==2)(void)file_security_wrong_realpath();
        else (void)file_security_realpath(kind==0?(char *)memory+0x11100:"/",kind==1?(char *)memory+0x13000:host);
        _exit(90);}
    int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
#include "fixtures/file_metadata_cases.h"
int main(int argc,char **argv){
    if(argc==4&&!strcmp(argv[1],"--child")){
        int fd=atoi(argv[2]),inherit=atoi(argv[3]);
        if(inherit){char data[6];assert(pread(fd,data,sizeof data,0)==6&&!memcmp(data,"TRIBES",6));}
        else {assert(fcntl(fd,F_GETFD)<0&&errno==EBADF);}
        return 0;
    }
    assert(argc==1);
    memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
    base=(uintptr_t)memory;assert(!mprotect(memory+0x10000,0x20000,PROT_READ|PROT_WRITE));
#ifdef CHECKED
    assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
    tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
    char root[]="/tmp/tribes-security-XXXXXX",path[4096];assert(mkdtemp(root));
    snprintf(path,sizeof path,"%s/public.store",root);
    int fd=open(path,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600);
    assert(fd>=0&&write(fd,"TRIBES",6)==6&&!fstat(fd,&fixture)&&!close(fd));
    assert(tribes_filesystem_configure(base,root,root,root));
    const uint16_t name[]={'p','u','b','l','i','c','.','s','t','o','r','e',0};
    memcpy(memory+0x11000,name,sizeof name);strcpy((char *)memory+0x11100,"public.store");
    /* Include an inaccessible descriptor pointer: OPEN_EXISTING must not read it. */
    const uint32_t cases[][3]={{12,0,0},{12,0,1},{12,0,42},{12,0xfffffff0,1},{12,0xfffffff0,0}};
    unsigned opens=0;
    for(unsigned wide=0;wide<2;wide++)for(unsigned i=0;i<6;i++){
        uint32_t security=i?0x12003:0,inherit=i&&cases[i-1][2]!=0;
        if(security)memcpy(memory+security,cases[i-1],12);
        _cpu cpu;memset(&cpu,0x39,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x1d003;
        uint32_t args[]={wide?0x11000:0x11100,0x80000000,1,security,3,0x80,0};
        memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;tribes_set_last_error(1234);
        tribes_enter(&cpu,wide?2:1);assert(cpu._eax!=UINT32_MAX&&tribes_get_last_error()==1234);
        expected._eax=cpu._eax;expected._esp+=sizeof args;
        assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0x1d003,args,sizeof args));
        if(security)assert(!memcmp(memory+security,cases[i-1],12));
        fd=owner();assert(fd>=0&&!!(fcntl(fd,F_GETFD)&FD_CLOEXEC)==!inherit);
        inherited_after_exec(argv[0],fd,inherit);
        char contents[6];assert(pread(fd,contents,sizeof contents,0)==6&&!memcmp(contents,"TRIBES",6));
        assert(tribes_close_handle(cpu._eax)&&owner()==-1);opens++;
    }
    uint32_t bad[]={24,0,1};memcpy(memory+0x12003,bad,12);
    assert(tribes_create_file_w(base,0x11000,0x80000000,1,0x12003,3,0x80,0)==UINT32_MAX&&tribes_get_last_error()==87&&owner()==-1);
    assert(tribes_create_file_w(base,0x11000,0x80000000,1,UINT32_MAX-10,3,0x80,0)==UINT32_MAX&&tribes_get_last_error()==87&&owner()==-1);
    metadata_cases(root);
    /* The original absent-store request must retain its genuine file-not-found result. */
    memcpy(memory+0x12003,cases[1],12);assert(!unlink(path));
    assert(tribes_create_file_w(base,0x11000,0x80000000,1,0x12003,3,0x80,0)==UINT32_MAX&&tribes_get_last_error()==2&&owner()==-1);
#ifdef CHECKED
    assert(tribes_memory_permissions_set(0x12000,4096,0));fault(0x12003);
    assert(tribes_memory_permissions_set(0x12000,4096,3));
    memcpy(memory+0x12ff8,cases[1],12);
    assert(tribes_memory_permissions_set(0x13000,4096,0));fault(0x12ff8);
    for(unsigned i=0;i<3;i++)external_fault(i);
#else
    (void)fault;(void)external_fault;
#endif
    assert(!rmdir(root));
    printf("PASS: %u emitted A/W opens preserve CPU and security bytes, inherited/noninherited descriptors verified through exec, ignored descriptor pointer, invalid layout/overflow and genuine missing-file result; A/W metadata-only access, delete sharing and duplicate lifetime, directories and final/dangling/escaping link objects%s\n",opens,
#ifdef CHECKED
           "; two protected security-structure reads and three invalid realpath calls rejected"
#else
           ""
#endif
    );
    return 0;
}
