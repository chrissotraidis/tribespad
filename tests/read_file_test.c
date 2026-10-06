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
#include <unistd.h>
#if !TARGET_OS_SIMULATOR
#include <signal.h>
#include <sys/wait.h>
#endif
extern void tribes_enter(_cpu *,uint32_t);
extern ssize_t unsafe_read_output(int,void *,size_t);
extern int unsafe_read_abi(int,void *);
static unsigned char *memory;static uint64_t base;
static uint32_t invoke(uint32_t h,uint32_t data,uint32_t bytes,uint32_t output,uint32_t async){
 _cpu cpu;memset(&cpu,0x59,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x98003;
 uint32_t args[]={h,data,bytes,output,async};memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;
 tribes_enter(&cpu,1);expected._eax=cpu._eax;expected._esp+=sizeof args;
 assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0x98003,args,sizeof args));return cpu._eax;
}
static uint32_t count(void){uint32_t value;memcpy(&value,memory+0x10001,4);return value;}
static void fault(const char *kind,uint32_t h,int fd){
 if(!strcmp(kind,"host-output")){unsafe_read_output(fd,memory+0x20ffc,16);return;}
 if(!strcmp(kind,"host-abi")){unsigned char byte;unsafe_read_abi(fd,&byte);return;}
 if(!strcmp(kind,"overlapped")){invoke(h,0x20ffc,16,0x10001,0x18000);return;}
 if(!strcmp(kind,"output"))assert(tribes_memory_permissions_set(0x10000,4096,1));
 else if(!strcmp(kind,"cross"))assert(tribes_memory_permissions_set(0x21000,4096,1));
 else assert(tribes_memory_permissions_set(0x20000,4096,1));
 invoke(h,0x20ffc,16,0x10001,0);
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x90000,PROT_READ|PROT_WRITE));
#ifdef CHECKED
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
 tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 char path[]="/tmp/tribes-read-XXXXXX";int fd=mkstemp(path);assert(fd>=0&&!unlink(path));
 const size_t size=131079;unsigned char *content=malloc(size);assert(content);
 for(size_t i=0;i<size;i++)content[i]=(unsigned char)((i*43+(i>>8))&255);
 assert(write(fd,content,size)==(ssize_t)size&&lseek(fd,0,SEEK_SET)==0);
 uint32_t h=tribes_file_fd_adopt(dup(fd),TRIBES_FILE_READ,7);assert(h);
 if(argc==2){fault(argv[1],h,fd);return 99;}assert(argc==1);
 /* Odd guest offsets and three native chunks, including embedded zero bytes. */
 memset(memory+0x20000,0xa3,size+32);tribes_set_last_error(5123);
 assert(invoke(h,0x20003,(uint32_t)size,0x10001,0)&&count()==size&&tribes_get_last_error()==5123);
 assert(!memcmp(memory+0x20003,content,size));for(unsigned i=0;i<3;i++)assert(memory[0x20000+i]==0xa3);
 for(unsigned i=0;i<29;i++)assert(memory[0x20003+size+i]==0xa3);
 assert(lseek(fd,0,SEEK_CUR)==(off_t)size);
 memset(memory+0x20003,0x73,32);assert(invoke(h,0x20003,32,0x10001,0)&&!count()&&tribes_get_last_error()==5123);
 for(unsigned i=0;i<32;i++)assert(memory[0x20003+i]==0x73);
 assert(lseek(fd,-7,SEEK_END)==(off_t)size-7);assert(invoke(h,0x20003,32,0x10001,0)&&count()==7);
 assert(!memcmp(memory+0x20003,content+size-7,7));for(unsigned i=7;i<32;i++)assert(memory[0x20003+i]==0x73);
 assert(invoke(h,0,0,0x10001,0)&&!count());assert(invoke(h,UINT32_MAX,0,0x10001,0)&&!count());
 assert(!invoke(h,UINT32_MAX,2,0x10001,0)&&!count()&&tribes_get_last_error()==87);
 assert(!invoke(h,0x20003,16,0,0)&&tribes_get_last_error()==87);
 assert(!invoke(h,0x20003,16,UINT32_MAX,0)&&tribes_get_last_error()==87);
 assert(!invoke(0,0x20003,16,0x10001,0)&&!count()&&tribes_get_last_error()==6);
 uint32_t metadata=tribes_file_fd_adopt(dup(fd),TRIBES_FILE_READ_ATTRIBUTES,7);assert(metadata);
 assert(!invoke(metadata,0x20003,16,0x10001,0)&&!count()&&tribes_get_last_error()==5);assert(tribes_close_handle(metadata));
 int pipefd[2];assert(!pipe(pipefd));uint32_t ph=tribes_fd_adopt(pipefd[0]);assert(ph);
 assert(!invoke(ph,0x20003,16,0x10001,0)&&!count()&&tribes_get_last_error()==1);assert(tribes_close_handle(ph)&&!close(pipefd[1]));
#if defined(CHECKED) && !TARGET_OS_SIMULATOR
 const char *faults[]={"output","buffer","cross","host-output","host-abi","overlapped"};
 for(unsigned i=0;i<sizeof faults/sizeof faults[0];i++){
  assert(lseek(fd,0,SEEK_SET)==0);memset(memory+0x10001,0x73,4);memset(memory+0x20000,0x73,8192);
  pid_t child=fork();assert(child>=0);if(!child){freopen("/dev/null","w",stderr);fault(faults[i],h,fd);_exit(99);}
  int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  assert(lseek(fd,0,SEEK_CUR)==0);assert(count()==(i==1||i==2?0:0x73737373));
  for(unsigned j=0;j<8192;j++)assert(memory[0x20000+j]==0x73);
 }
#endif
 assert(tribes_close_handle(h)&&!close(fd));free(content);
 puts("PASS: checked synchronous regular-file reads through host buffers; multi-chunk bytes, EOF/short read, offsets, zero/error/access behavior, CPU/stdcall/LastError and sentinel preservation");return 0;
}
