#include "../port/core/guest_heap.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t),tbimp_CreateFileA(void),tbimp_SetFileTime(void),tribes_return_to_host(void);
_Noreturn void tribes_unimplemented_dinput(void *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static unsigned char *memory;
static uint32_t invoke(unsigned entry,const uint32_t *args,unsigned count,uint32_t error){
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x100803;
 memset(memory+0x1007b7,0x79,64);memcpy(memory+cpu._esp,args,4*count);memset(memory+cpu._esp+4*count,0x79,64);_cpu before=cpu;
 tribes_set_last_error(123);tribes_enter(&cpu,entry);before._eax=cpu._eax;before._esp+=4*count;
 assert(!memcmp(&cpu,&before,sizeof cpu)&&!memcmp(memory+0x100803,args,4*count));
 for(unsigned i=0;i<64;i++)assert(memory[0x1007b7+i]==0x79&&memory[0x100803+4*count+i]==0x79);
 assert(tribes_get_last_error()==error);return cpu._eax;
}
static uint32_t open_guest(const char *name,uint32_t access,uint32_t flags){
 strcpy((char *)memory+0x300001,name);uint32_t args[]={0x300001,access,7,0,3,flags,0};uint32_t h=invoke(1,args,7,123);assert(h!=UINT32_MAX);return h;
}
static void set(uint32_t handle,uint32_t creation,uint32_t access,uint32_t write,uint32_t error){uint32_t args[]={handle,creation,access,write};assert(invoke(2,args,4,error)==(error==123));}
static struct stat metadata(const char *path){struct stat st;assert(!stat(path,&st));return st;}
static void same_times(struct stat a,struct stat b){assert(a.st_birthtimespec.tv_sec==b.st_birthtimespec.tv_sec&&a.st_birthtimespec.tv_nsec==b.st_birthtimespec.tv_nsec&&a.st_atimespec.tv_sec==b.st_atimespec.tv_sec&&a.st_atimespec.tv_nsec==b.st_atimespec.tv_nsec&&a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec&&a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec);}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));assert(tribes_memory_permissions_set(0,UINT64_C(0x100000000),0));
 for(unsigned p=0x100000;p<=0x300000;p+=0x100000){assert(!mprotect(memory+p,4096,PROT_READ|PROT_WRITE));assert(tribes_memory_permissions_set(p,4096,3));}
 const void *targets[]={(void *)tbimp_CreateFileA,(void *)tbimp_SetFileTime,(void *)tribes_return_to_host};tribes_memory_register_targets(targets,3);tribes_memory_external_configure((uintptr_t)memory,UINT64_C(0x100000000));tribes_memory_external_enable();
 char root[]="/tmp/tribes-filetime-XXXXXX",path[4096],directory[4096];assert(mkdtemp(root));snprintf(path,sizeof path,"%s/cache",root);snprintf(directory,sizeof directory,"%s/folder",root);
 int fd=open(path,O_CREAT|O_EXCL|O_RDWR,0600);assert(fd>=0&&write(fd,"TRIBES",6)==6&&!close(fd)&&!mkdir(directory,0700));assert(tribes_filesystem_configure((uintptr_t)memory,root,root,root));
 /* Independent Unix UTC dates: 2023/2024/2025 January 1, plus fractional ns.
    Access/write inputs match the original call's two adjacent FILETIMEs. */
 uint64_t times[]={UINT64_C(133170048001234567),UINT64_C(133801632005551111),UINT64_C(133485408009876543)};
 memcpy(memory+0x200001,times,sizeof times);uint32_t h=open_guest("cache",0x40000000,0);
 if(argc>1){uint32_t input=!strcmp(argv[1],"short")?0x200ffd:0x200001;
  if(!strcmp(argv[1],"protected")){assert(!mprotect(memory+0x200000,4096,PROT_NONE));assert(tribes_memory_permissions_set(0x200000,4096,0));}
  set(h,0,input,0,123);return 99;
 }
 set(h,0x200001,0x200009,0x200011,123);struct stat st=metadata(path);
 assert(st.st_birthtimespec.tv_sec==1672531200&&st.st_birthtimespec.tv_nsec==123456700);
 assert(st.st_atimespec.tv_sec==1735689600&&st.st_atimespec.tv_nsec==555111100);
 assert(st.st_mtimespec.tv_sec==1704067200&&st.st_mtimespec.tv_nsec==987654300);
 set(h,0,0,0,123);same_times(st,metadata(path));uint64_t zero=0;memcpy(memory+0x200001,&zero,8);set(h,0x200001,0x200001,0x200001,123);same_times(st,metadata(path));
 uint64_t invalid=UINT64_C(0x8000000000000000);memcpy(memory+0x200001,&invalid,8);set(h,0,0x200001,0,87);same_times(st,metadata(path));invalid=UINT64_MAX;memcpy(memory+0x200001,&invalid,8);set(h,0,0x200001,0,50);same_times(st,metadata(path));set(h,0,0xfffffffdu,0,87);same_times(st,metadata(path));
 memcpy(memory+0x200ff8,times+1,8);assert(!mprotect(memory+0x200000,4096,PROT_READ));assert(tribes_memory_permissions_set(0x200000,4096,1));set(h,0,0,0x200ff8,123);struct stat changed=metadata(path);assert(changed.st_mtimespec.tv_sec==1735689600&&changed.st_mtimespec.tv_nsec==555111100&&changed.st_birthtimespec.tv_sec==st.st_birthtimespec.tv_sec);
 assert(!mprotect(memory+0x200000,4096,PROT_READ|PROT_WRITE));assert(tribes_memory_permissions_set(0x200000,4096,3));
 uint32_t ro=open_guest("cache",0x80000000,0);set(ro,0,0,0x200ff8,5);same_times(changed,metadata(path));assert(tribes_close_handle(ro));
 assert(tribes_close_handle(h));set(h,0,0,0,6);set(0xdeadbeef,0,0,0,6);
 h=open_guest("cache",0x100,0);set(h,0,0x200ff8,0x200ff8,123);assert(tribes_close_handle(h));
 h=open_guest("folder",0x100,0x02000000);set(h,0,0,0x200ff8,123);st=metadata(directory);assert(st.st_mtimespec.tv_sec==1735689600&&st.st_mtimespec.tv_nsec==555111100);assert(tribes_close_handle(h));
 fd=open(path,O_RDONLY);char content[6];assert(fd>=0&&read(fd,content,6)==6&&!memcmp(content,"TRIBES",6)&&!close(fd));assert(!unlink(path)&&!rmdir(directory)&&!rmdir(root));assert(!munmap(memory,UINT64_C(0x100000000)));
 puts("PASS: emitted SetFileTime changes actual native creation/access/write timestamps and 100 ns precision; generic-write/attribute-only/directory rights, null/zero preservation, invalid/suppression rejection, readonly/page-end input, unchanged contents and full CPU/stack/LastError");return 0;
}
