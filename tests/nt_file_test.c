#include "../port/core/guest_file.h"
#include "../port/core/guest_filesystem.h"
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
static unsigned char *memory;static uint64_t base;
static uint32_t u32(unsigned p){uint32_t v;memcpy(&v,memory+p,4);return v;}
static uint64_t u64(unsigned p){uint64_t v;memcpy(&v,memory+p,8);return v;}
static uint64_t stamp(struct timespec t){return ((uint64_t)t.tv_sec+11644473600ULL)*10000000+(uint64_t)t.tv_nsec/100;}
static uint32_t invoke(unsigned api,uint32_t h,uint32_t io,uint32_t out,uint32_t size,uint32_t kind){
 _cpu cpu;memset(&cpu,0x59,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x28003;
 uint32_t args[]={h,io,out,size,kind};memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;
 tribes_set_last_error(0xabc123);tribes_enter(&cpu,api);
 expected._eax=cpu._eax;expected._esp+=sizeof args;assert(!memcmp(&cpu,&expected,sizeof cpu));
 assert(!memcmp(memory+0x28003,args,sizeof args)&&tribes_get_last_error()==0xabc123);return cpu._eax;
}
static uint32_t open_guest(const char *name,uint32_t access,uint32_t flags){
 strcpy((char *)memory+0x18000,name);
 uint32_t h=tribes_create_file(base,0x18000,access,7,0,3,flags,0);assert(h!=UINT32_MAX);return h;
}
static void cleared(void){memset(memory+0x10000,0x5a,32);memset(memory+0x12000,0x5a,8192);}
static void unchanged(unsigned api,uint32_t h,uint32_t io,uint32_t out,uint32_t size,uint32_t kind,uint32_t status){
 cleared();assert(invoke(api,h,io,out,size,kind)==status);
 for(unsigned i=0;i<32;i++)assert(memory[0x10000+i]==0x5a);
 for(unsigned i=0;i<8192;i++)assert(memory[0x12000+i]==0x5a);
}
static void file(uint32_t h,int fd,const char *name,uint32_t rights,uint32_t mode,uint32_t capacity){
 struct stat s;assert(!fstat(fd,&s));off_t position=lseek(fd,0,SEEK_CUR);assert(position>=0);
 unsigned out=0x12004,nb=(unsigned)strlen(name)*2,copied=capacity-100;
 if(copied>nb)copied=nb;copied&=~1u;
 uint32_t status=copied<nb?0x80000005:0;cleared();
 assert(invoke(1,h,0x10004,out,capacity,18)==status);
 assert(u32(0x10004)==status&&u32(0x10008)==100+copied);
 assert(u64(out)==stamp(s.st_birthtimespec)&&u64(out+8)==stamp(s.st_atimespec));
 assert(u64(out+16)==stamp(s.st_mtimespec)&&u64(out+24)==stamp(s.st_ctimespec));
 assert(u32(out+32)==(uint32_t)((S_ISDIR(s.st_mode)?16:32)|(!(s.st_mode&S_IWUSR)?1:0)|((s.st_flags&UF_HIDDEN)?2:0)));
 assert(u64(out+40)==(S_ISDIR(s.st_mode)?0:(uint64_t)s.st_blocks*512));
 assert(u64(out+48)==(S_ISDIR(s.st_mode)?0:(uint64_t)s.st_size));
 assert(u32(out+56)==s.st_nlink&&!memory[out+60]&&memory[out+61]==(S_ISDIR(s.st_mode)!=0));
 assert(u64(out+64)==s.st_ino&&!u32(out+72)&&u32(out+76)==rights);
 assert(u64(out+80)==(uint64_t)position&&u32(out+88)==mode&&!u32(out+92)&&u32(out+96)==nb);
 for(unsigned i=0;i<copied;i++)assert(memory[out+100+i]==(i&1?0:(unsigned char)name[i/2]));
 for(unsigned i=0;i<32;i++)if(i<4||i>=12)assert(memory[0x10000+i]==0x5a);
 for(unsigned i=0;i<8192;i++)if(i<4||i>=4+100+copied)assert(memory[0x12000+i]==0x5a);
 assert(lseek(fd,0,SEEK_CUR)==position);
}
static void volume(uint32_t h,int fd,unsigned capacity){
 struct statfs fs;assert(!fstatfs(fd,&fs));const char *label="TribesPad";
 uint32_t n=capacity<36?capacity&~1u:36,status=n<36?0x80000005:0;
 cleared();assert(invoke(2,h,0x10004,0x12004,capacity,1)==status);
 assert(u32(0x10004)==status&&u32(0x10008)==n);
 assert(!u64(0x12004)&&u32(0x1200c)==(uint32_t)fs.f_fsid.val[0]&&u32(0x12010)==18);
 assert(!memory[0x12014]&&!memory[0x12015]);
 for(unsigned i=18;i<n;i++)assert(memory[0x12004+i]==(i&1?0:(unsigned char)label[(i-18)/2]));
 for(unsigned i=0;i<8192;i++)if(i<4||i>=4+n)assert(memory[0x12000+i]==0x5a);
}
static void fault(const char *kind,uint32_t h){
 if(!strcmp(kind,"class"))invoke(1,h,0x10004,0x12004,104,4);
 else {
  unsigned api=!strncmp(kind,"volume-",7)?2:1;const char *which=api==2?kind+7:kind;
  uint32_t out=!strcmp(which,"cross")?0x12ff0:0x12004;
  assert(tribes_memory_permissions_set(!strcmp(which,"iosb")?0x10000:!strcmp(which,"cross")?0x13000:0x12000,4096,1));
  invoke(api,h,0x10004,out,512,api==2?1:18);
 }
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x20000,PROT_READ|PROT_WRITE));
#ifdef CHECKED
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
 tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 char root[]="/tmp/tribes-nt-file-XXXXXX",path[PATH_MAX],other[PATH_MAX];assert(mkdtemp(root));
 snprintf(path,sizeof path,"%s/Certificate.crt",root);int fd=open(path,O_CREAT|O_EXCL|O_RDWR,0600);assert(fd>=0);
 assert(write(fd,"TRIBES",6)==6&&lseek(fd,2,SEEK_SET)==2);
 assert(tribes_filesystem_configure(base,root,root,root));
 uint32_t h=open_guest(".\\certificate.CRT",0x80000000,0x08000000);
 if(argc==2){fault(argv[1],h);return 99;}assert(argc==1);
 int queryfd=tribes_fd_borrow(h);assert(queryfd>=0&&lseek(queryfd,3,SEEK_SET)==3);
 const char *name="\\TribesPad\\Game\\Certificate.crt";
 for(unsigned size=104;size<170;size++)file(h,queryfd,name,0x120089,0x24,size);
 file(h,queryfd,name,0x120089,0x24,UINT32_MAX);
 for(unsigned size=24;size<45;size++)volume(h,fd,size);
 for(unsigned size=0;size<104;size++)unchanged(1,h,0x10004,0x12004,size,18,0xc0000004);
 for(unsigned size=0;size<24;size++)unchanged(2,h,0x10004,0x12004,size,1,0xc0000004);
 unchanged(1,h,0,0x12004,104,18,0xc0000005);unchanged(1,h,0x10004,0,104,18,0xc0000005);
 unchanged(1,h,0xfffffffc,0x12004,104,18,0xc0000005);unchanged(1,h,0x10004,0xfffffffc,104,18,0xc0000005);
 unchanged(1,h,0x10006,0x12004,104,18,0x80000002);unchanged(1,h,0x10004,0x12006,104,18,0x80000002);
 unchanged(1,0,0x10004,0x12004,104,18,0xc0000008);unchanged(1,UINT32_MAX,0x10004,0x12004,104,18,0xc0000024);
 unchanged(1,h,0x10004,0x12004,104,0,0xc0000003);
 uint32_t raw=tribes_fd_adopt(dup(fd));assert(raw);unchanged(1,raw,0x10004,0x12004,104,18,0xc00000bb);assert(tribes_close_handle(raw));
 uint32_t meta=open_guest("Certificate.crt",0x10180,0x02200000);int mf=tribes_fd_borrow(meta);assert(mf>=0);
 file(meta,mf,name,0x110180,0x20,512);assert(!tribes_read_file(base,meta,0x19000,1,0x18000,0)&&tribes_get_last_error()==5);
 assert(tribes_duplicate_handle(base,UINT32_MAX,meta,UINT32_MAX,0x18000,0,0,2));uint32_t copy=u32(0x18000);
 assert(tribes_close_handle(meta));file(copy,mf,name,0x110180,0x20,512);assert(tribes_close_handle(copy)&&!close(mf));
 uint32_t wh=open_guest("Certificate.crt",0x40000000,0);int wf=tribes_fd_borrow(wh);assert(wf>=0);file(wh,wf,name,0x120196,0x20,512);assert(tribes_close_handle(wh)&&!close(wf));
 uint32_t dir=open_guest("C:\\TribesPad\\Game",0,0x02000000);int df=tribes_fd_borrow(dir);assert(df>=0);
 file(dir,df,"\\TribesPad\\Game",0x100080,0x20,512);assert(tribes_close_handle(dir)&&!close(df));
 /* Opening under a different CWD resolves one canonical name; later CWD
    changes cannot mutate the immutable per-open metadata. */
 strcpy((char *)memory+0x18000,"C:\\");assert(tribes_set_current_directory(base,0x18000));
 file(h,queryfd,name,0x120089,0x24,512);
 snprintf(other,sizeof other,"%s/second.crt",root);assert(!link(path,other));file(h,queryfd,name,0x120089,0x24,512);assert(!unlink(other));
 assert(!fchmod(fd,0400));file(h,queryfd,name,0x120089,0x24,512);assert(!fchmod(fd,0600));
#if defined(CHECKED) && !TARGET_OS_SIMULATOR
 const char *faults[]={"output","iosb","cross","volume-output","volume-iosb","volume-cross","class"};
 for(unsigned i=0;i<sizeof faults/sizeof faults[0];i++){
  cleared();pid_t child=fork();assert(child>=0);if(!child){freopen("/dev/null","w",stderr);fault(faults[i],h);_exit(99);}
  int result;assert(waitpid(child,&result,0)==child&&WIFSIGNALED(result)&&WTERMSIG(result)==SIGABRT);
  for(unsigned j=0;j<32;j++)assert(memory[0x10000+j]==0x5a);
  for(unsigned j=0;j<8192;j++)assert(memory[0x12000+j]==0x5a);
 }
#endif
 assert(!rename(path,other));unchanged(1,h,0x10004,0x12004,512,18,0xc00000bb);
 assert(tribes_close_handle(h));unchanged(1,h,0x10004,0x12004,104,18,0xc0000008);
 assert(!close(queryfd)&&!close(fd)&&!unlink(other)&&!rmdir(root));
 puts("PASS: real file/directory metadata, times/size/inode/offset/links, retained rights/mode/path and duplicates, full/truncated UTF16 names and virtual-volume label, CPU/stdcall/LastError, exact output lengths and unchanged error/fault buffers");return 0;
}
