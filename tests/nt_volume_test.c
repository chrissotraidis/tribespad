#include "../port/core/guest_file.h"
#include "../port/core/guest_heap.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#if !TARGET_OS_SIMULATOR
#include <signal.h>
#include <sys/wait.h>
#endif
extern void tribes_enter(_cpu *,uint32_t);
extern int unsafe_volume_output(int,void *);
extern uint64_t unsafe_volume_abi(int,void *);
static unsigned char *memory;static uint64_t base;
static uint32_t invoke(uint32_t h,uint32_t io,uint32_t out,uint32_t size,uint32_t kind){
 _cpu cpu;memset(&cpu,0x59,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x28003;
 uint32_t args[]={h,io,out,size,kind};memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;
 tribes_set_last_error(0xabc123);tribes_enter(&cpu,1);
 expected._eax=cpu._eax;expected._esp+=sizeof args;assert(!memcmp(&cpu,&expected,sizeof cpu));
 assert(!memcmp(memory+0x28003,args,sizeof args)&&tribes_get_last_error()==0xabc123);return cpu._eax;
}
static void success(uint32_t h,int fd){
 struct statfs fs;assert(!fstatfs(fd,&fs));uint32_t want[2]={fs.f_flags&MNT_LOCAL?8:0x14,(fs.f_flags&MNT_RDONLY?2:0)|(fs.f_flags&MNT_LOCAL?0:0x10)};
 memset(memory+0x10000,0x5a,32);memset(memory+0x12000,0x5a,32);
 assert(!invoke(h,0x10004,0x12004,16,4));uint32_t io[2]={0,8};
 assert(!memcmp(memory+0x10004,io,8)&&!memcmp(memory+0x12004,want,8));
 for(unsigned i=0;i<32;i++)if(i<4||i>=12)assert(memory[0x10000+i]==0x5a&&memory[0x12000+i]==0x5a);
}
static void unchanged_error(uint32_t h,uint32_t io,uint32_t out,uint32_t size,uint32_t kind,uint32_t status){
 memset(memory+0x10000,0x61,32);memset(memory+0x12000,0x61,32);assert(invoke(h,io,out,size,kind)==status);
 for(unsigned i=0;i<32;i++)assert(memory[0x10000+i]==0x61&&memory[0x12000+i]==0x61);
}
static void fault(const char *kind,uint32_t h,int fd){
 if(!strcmp(kind,"host-output"))unsafe_volume_output(fd,memory+0x12004);
 else if(!strcmp(kind,"host-abi")){struct statfs fs;unsafe_volume_abi(fd,&fs);}
 else if(!strcmp(kind,"device")){int pipes[2];assert(!pipe(pipes));uint32_t ph=tribes_fd_adopt(pipes[0]);assert(ph);invoke(ph,0x10004,0x12004,8,4);assert(tribes_close_handle(ph)&&!close(pipes[1]));}
 else if(!strcmp(kind,"class"))invoke(h,0x10004,0x12004,8,2);
 else {
  uint32_t out=0x12004;
  if(!strcmp(kind,"iosb"))assert(tribes_memory_permissions_set(0x10000,4096,1));
  else if(!strcmp(kind,"cross")){out=0x12ffc;assert(tribes_memory_permissions_set(0x13000,4096,1));}
  else assert(tribes_memory_permissions_set(0x12000,4096,1));
  invoke(h,0x10004,out,8,4);
 }
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x20000,PROT_READ|PROT_WRITE));
#ifdef CHECKED
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
 tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 char path[]="/tmp/tribes-volume-XXXXXX";int fd=mkstemp(path);assert(fd>=0&&!unlink(path));
 assert(write(fd,"TRIBES",6)==6&&lseek(fd,2,SEEK_SET)==2);
 uint32_t h=tribes_file_fd_adopt(dup(fd),TRIBES_FILE_READ_ATTRIBUTES,7);assert(h);
 if(argc==2){fault(argv[1],h,fd);return 99;}assert(argc==1);
 success(h,fd);assert(lseek(fd,0,SEEK_CUR)==2);
 for(unsigned len=0;len<8;len++)unchanged_error(h,0x10004,0x12004,len,4,0xc0000004);
 unchanged_error(0,0,0,0,4,0xc0000004);
 unchanged_error(h,0x10004,0x12004,8,0,0xc0000003);
 unchanged_error(h,0,0x12004,8,4,0xc0000005);unchanged_error(h,0x10004,0,8,4,0xc0000005);
 unchanged_error(h,0xfffffffcu,0x12004,8,4,0xc0000005);unchanged_error(h,0x10004,0xfffffffcu,8,4,0xc0000005);
 unchanged_error(h,0x10006,0x12004,8,4,0x80000002);unchanged_error(h,0x10004,0x12006,8,4,0x80000002);
 unchanged_error(0,0x10004,0x12004,8,4,0xc0000008);
 unchanged_error(UINT32_MAX,0x10004,0x12004,8,4,0xc0000024);
 unchanged_error(UINT32_MAX-1,0x10004,0x12004,8,4,0xc0000024);
 uint32_t sem=tribes_create_semaphore(base,0,0,1,0);assert(sem);
 unchanged_error(sem,0x10004,0x12004,8,4,0xc0000024);assert(tribes_close_handle(sem));
 int dir=open("/tmp",O_RDONLY|O_DIRECTORY);assert(dir>=0);uint32_t dh=tribes_file_fd_adopt(dup(dir),0,7);assert(dh);success(dh,dir);assert(tribes_close_handle(dh)&&!close(dir));
 char linkroot[]="/tmp/tribes-volume-link-XXXXXX",linkpath[512];assert(mkdtemp(linkroot));
 snprintf(linkpath,sizeof linkpath,"%s/link",linkroot);assert(!symlink("/",linkpath));
 int linkfd=open(linkpath,O_EVTONLY|O_SYMLINK);assert(linkfd>=0);uint32_t lh=tribes_file_fd_adopt(dup(linkfd),0,7);assert(lh);
 success(lh,linkfd);assert(tribes_close_handle(lh)&&!close(linkfd)&&!unlink(linkpath)&&!rmdir(linkroot));
 /* A retained native borrow survives removal of the public handle. */
 uint32_t status=1;int borrowed=tribes_fd_borrow_nt(h,&status);assert(borrowed>=0&&!status);assert(tribes_close_handle(h));
 struct statfs fs;assert(!fstatfs(borrowed,&fs));unchanged_error(h,0x10004,0x12004,8,4,0xc0000008);
 h=tribes_file_fd_adopt(borrowed,0,7);assert(h);success(h,fd);
#if defined(CHECKED) && !TARGET_OS_SIMULATOR
 const char *faults[]={"output","iosb","cross","host-output","host-abi","class","device"};
 for(unsigned i=0;i<sizeof faults/sizeof faults[0];i++){
  memset(memory+0x10000,0x73,32);memset(memory+0x12000,0x73,8192);
  pid_t child=fork();assert(child>=0);if(!child){freopen("/dev/null","w",stderr);fault(faults[i],h,fd);_exit(99);}
  int result;assert(waitpid(child,&result,0)==child&&WIFSIGNALED(result)&&WTERMSIG(result)==SIGABRT);
  for(unsigned j=0;j<32;j++)assert(memory[0x10000+j]==0x73);
  for(unsigned j=0;j<8192;j++)assert(memory[0x12000+j]==0x73);
 }
#endif
 assert(tribes_close_handle(h)&&!close(fd));
 puts("PASS: actual descriptor/mount query, file, directory and symlink object, metadata-only rights, exact x86 records and stdcall/CPU/LastError preservation, bounds/length/type errors, unchanged error outputs, retained borrow lifetime, file offset preservation");return 0;
}
