#include "../port/core/guest_file.h"
#include "../port/core/guest_heap.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tbimp_WriteFile(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
extern long unsafe_write(int,const void *,unsigned long);
static unsigned char *memory;static uint64_t base;static uint32_t output=0x10201;
static uint32_t count(void){uint32_t n;memcpy(&n,memory+output,4);return n;}
static void rejected_write(uint32_t handle,int fd,unsigned kind){
 assert(!ftruncate(fd,0)&&lseek(fd,0,SEEK_SET)==0);memset(memory+output,0xa7,4);
 if(kind==0)assert(tribes_memory_permissions_set(0x10000,4096,1));
 if(kind==1)assert(tribes_memory_permissions_set(0x13000,4096,0));
 pid_t child=fork();assert(child>=0);if(!child){if(kind==2)unsafe_write(fd,memory+0x12ff0,32);else tribes_write_file(base,handle,0x12ff0,32,output,0);_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);struct stat st;assert(!fstat(fd,&st)&&st.st_size==0);
 if(kind==0){assert(count()==0xa7a7a7a7);assert(tribes_memory_permissions_set(0x10000,4096,3));}else if(kind==1){assert(count()==0);assert(tribes_memory_permissions_set(0x13000,4096,3));}
}
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(!mprotect(memory+0x10000,0x10000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
 /* Original seven-argument console-open request through generated guest ABI.
    Host standard streams exist, but do not attach a Windows console. */
 const uint16_t console_name[]={'C','O','N','O','U','T','$',0};memcpy(memory+0x11003,console_name,sizeof console_name);
 _cpu console_cpu;memset(&console_cpu,0x39,sizeof console_cpu);console_cpu._pointer_offset=base;console_cpu._esp=0x1d001;
 uint32_t console_args[]={0x11003,0xc0000000,2,0,3,0,0};memcpy(memory+console_cpu._esp,console_args,sizeof console_args);
 _cpu console_expected=console_cpu;console_expected._eax=UINT32_MAX;console_expected._esp+=sizeof console_args;
 tribes_enter(&console_cpu,2);assert(!memcmp(&console_cpu,&console_expected,sizeof console_cpu)&&tribes_get_last_error()==6&&!memcmp(memory+0x11003,console_name,sizeof console_name));
 assert(tribes_memory_permissions_set(0x11000,4096,0));pid_t child=fork();assert(child>=0);if(!child){tribes_create_file_w(base,0x11003,0xc0000000,2,0,3,0,0);_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(tribes_memory_permissions_set(0x11000,4096,3));
 char path[]="/tmp/tribes-write-XXXXXX";int fd=mkstemp(path);assert(fd>=0&&!unlink(path));uint32_t handle=tribes_file_fd_adopt(dup(fd),3,3);assert(handle);
 memcpy(memory+0x11003,"TRIBES\0PAD",10);tribes_set_last_error(12345);assert(tribes_write_file(base,handle,0x11003,10,output,0)&&count()==10&&tribes_get_last_error()==12345);char data[32]={0};assert(lseek(fd,0,SEEK_SET)==0&&read(fd,data,32)==10&&!memcmp(data,"TRIBES\0PAD",10));
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x1e001;uint32_t args[5]={handle,0x11003,10,output,0};cpu._esp-=20;memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;tribes_enter(&cpu,1);expected._eax=1;expected._esp+=20;assert(!memcmp(&cpu,&expected,sizeof cpu)&&count()==10&&!memcmp(memory+cpu._esp-20,args,sizeof args));
 assert(tribes_write_file(base,handle,0,0,output,0)&&count()==0);struct stat st;assert(!fstat(fd,&st)&&st.st_size==20);
 assert(!tribes_write_file(base,0x123,0x11003,10,output,0)&&count()==0&&tribes_get_last_error()==6);
 assert(!tribes_write_file(base,handle,UINT32_MAX,2,output,0)&&count()==0&&tribes_get_last_error()==87);
 uint32_t readonly=tribes_file_fd_adopt(dup(fd),1,3);assert(readonly);assert(!tribes_write_file(base,readonly,0x11003,10,output,0)&&count()==0&&tribes_get_last_error()==5);assert(tribes_close_handle(readonly));
 int pipes[2];assert(!pipe(pipes));uint32_t writer=tribes_fd_adopt(pipes[1]);assert(writer);assert(tribes_write_file(base,writer,0x11003,10,output,0)&&count()==10);assert(read(pipes[0],data,32)==10&&!memcmp(data,"TRIBES\0PAD",10));assert(!close(pipes[0]));assert(!tribes_write_file(base,writer,0x11003,10,output,0)&&count()==0&&tribes_get_last_error()==109);assert(tribes_close_handle(writer));
 assert(!pipe(pipes)&&!fcntl(pipes[1],F_SETFL,O_NONBLOCK));writer=tribes_fd_adopt(pipes[1]);assert(writer);char fill[4096];memset(fill,0x49,sizeof fill);while(write(pipes[1],fill,sizeof fill)>0){}assert(errno==EAGAIN);assert(tribes_write_file(base,writer,0x11003,10,output,0)&&count()==0);assert(!close(pipes[0])&&tribes_close_handle(writer));
 rejected_write(handle,fd,0);rejected_write(handle,fd,1);rejected_write(handle,fd,2);assert(tribes_close_handle(handle)&&!close(fd));
 puts("PASS: detached-console CreateFileW ABI and protected UTF16 source rejection; real regular-file and pipe bytes, emitted WriteFile ABI, zero/access/error semantics, broken/full pipe behavior, protected output/source preservation and rejection of direct guest-buffer write");return 0;
}
