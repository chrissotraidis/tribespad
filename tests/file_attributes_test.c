#include "../port/core/guest_file.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/guest_heap.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;
static uint32_t invoke(unsigned wide,uint32_t name){
 _cpu cpu;memset(&cpu,0x59,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x28003;
 memcpy(memory+cpu._esp,&name,4);_cpu expected=cpu;tribes_enter(&cpu,wide?2:1);
 expected._eax=cpu._eax;expected._esp+=4;assert(!memcmp(&cpu,&expected,sizeof cpu));
 assert(!memcmp(memory+0x28003,&name,4));return cpu._eax;
}
static uint32_t make_directory(unsigned wide,uint32_t name){
 _cpu cpu={0};cpu._pointer_offset=base;cpu._esp=0x28003;uint32_t args[]={name,0};
 memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;tribes_enter(&cpu,wide?4:3);
 expected._eax=cpu._eax;expected._esp+=8;assert(!memcmp(&cpu,&expected,sizeof cpu));return cpu._eax;
}
static void create_check(unsigned wide,const char *name,uint32_t expected,uint32_t error){
 if(wide)for(size_t i=0;i<=strlen(name);i++){uint16_t ch=(unsigned char)name[i];memcpy(memory+0x10001+i*2,&ch,2);}
 else strcpy((char *)memory+0x10001,name);
 tribes_set_last_error(1234);assert(make_directory(wide,0x10001)==expected);assert(tribes_get_last_error()==error);
}
static uint32_t open_file(unsigned wide,const char *name,uint32_t access,uint32_t share,uint32_t disposition){
 if(wide)for(size_t i=0;i<=strlen(name);i++){uint16_t ch=(unsigned char)name[i];memcpy(memory+0x10001+i*2,&ch,2);}
 else strcpy((char *)memory+0x10001,name);
 _cpu cpu={0};cpu._pointer_offset=base;cpu._esp=0x28003;
 uint32_t args[]={0x10001,access,share,0,disposition,0x08000080,0};memcpy(memory+cpu._esp,args,sizeof args);
 _cpu expected=cpu;tribes_enter(&cpu,wide?6:5);expected._eax=cpu._eax;expected._esp+=sizeof args;
 assert(!memcmp(&cpu,&expected,sizeof cpu));return cpu._eax;
}
static void cache_file(unsigned wide,const char *root){
 uint32_t h=open_file(wide,"Cache.gui.dso",0x40000000,1,2);assert(h!=UINT32_MAX&&tribes_get_last_error()==0);
 memcpy(memory+0x12000,"original cache",14);assert(tribes_write_file(base,h,0x12000,14,0x20010,0));assert(tribes_close_handle(h));
 h=open_file(wide,"cache.gui.dso",0xc0000000,1,4);assert(h!=UINT32_MAX&&tribes_get_last_error()==183&&tribes_get_file_size(base,h,0)==14);assert(tribes_close_handle(h));
 uint32_t reader=open_file(wide,"CACHE.gui.dso",0x80000000,1,3);assert(reader!=UINT32_MAX);
 assert(open_file(wide,"cache.gui.dso",0x40000000,1,2)==UINT32_MAX&&tribes_get_last_error()==32);
 char path[PATH_MAX],bytes[14];snprintf(path,sizeof path,"%s/Cache.gui.dso",root);
 int fd=open(path,O_RDONLY);assert(fd>=0&&read(fd,bytes,sizeof bytes)==14&&!memcmp(bytes,"original cache",14));assert(!close(fd));
 assert(tribes_close_handle(reader));
 h=open_file(wide,"cAcHe.gui.dso",0x40000000,1,2);assert(h!=UINT32_MAX&&tribes_get_last_error()==183);
 struct stat st;assert(!stat(path,&st)&&st.st_size==0);assert(tribes_close_handle(h));
 assert(open_file(wide,"missing/cache.gui.dso",0x40000000,1,2)==UINT32_MAX&&tribes_get_last_error()==3);
 assert(open_file(wide,"link/forbidden.gui.dso",0x40000000,1,2)==UINT32_MAX&&tribes_get_last_error()==5);
 assert(open_file(wide,"link",0x40000000,1,2)==UINT32_MAX);
 assert(open_file(wide,"Folder",0x40000000,1,2)==UINT32_MAX);
 assert(open_file(wide,"bad?name",0x40000000,1,2)==UINT32_MAX&&tribes_get_last_error()==123);
 assert(!unlink(path));
 h=open_file(wide,"Cache.gui.dso",0x40000000,1,4);assert(h!=UINT32_MAX&&tribes_get_last_error()==0);assert(tribes_close_handle(h)&&!unlink(path));
}
static void check(unsigned wide,const char *name,uint32_t expected,uint32_t error){
 if(wide)for(size_t i=0;i<=strlen(name);i++){uint16_t ch=(unsigned char)name[i];memcpy(memory+0x10001+i*2,&ch,2);}
 else strcpy((char *)memory+0x10001,name);
 unsigned char copy[8192];size_t size=(strlen(name)+1)*(wide?2:1);assert(size<=sizeof copy);memcpy(copy,memory+0x10001,size);
 tribes_set_last_error(1234);assert(invoke(wide,0x10001)==expected);assert(tribes_get_last_error()==error);
 assert(!memcmp(copy,memory+0x10001,size));
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x20000,PROT_READ|PROT_WRITE));
#ifdef CHECKED
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
 tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
 if(argc==2){
  if(!strcmp(argv[1],"cross")){memory[0x10fff]='A';assert(tribes_memory_permissions_set(0x11000,4096,0));invoke(1,0x10fff);}
  else {assert(tribes_memory_permissions_set(0x10000,4096,0));if(!strncmp(argv[1],"create-",7))make_directory(!strcmp(argv[1],"create-wide"),0x10001);else invoke(!strcmp(argv[1],"wide"),0x10001);}
  return 99;
 }
#else
 (void)argv;
#endif
 assert(argc==1);
 char root[]="/tmp/tribes-attributes-XXXXXX",file[PATH_MAX],folder[PATH_MAX],linkpath[PATH_MAX],pipepath[PATH_MAX];assert(mkdtemp(root));
 snprintf(file,sizeof file,"%s/Asset.bin",root);snprintf(folder,sizeof folder,"%s/Folder",root);snprintf(linkpath,sizeof linkpath,"%s/link",root);snprintf(pipepath,sizeof pipepath,"%s/pipe",root);
 int fd=open(file,O_CREAT|O_EXCL|O_RDWR,0600);assert(fd>=0&&write(fd,"TRIBES",6)==6);assert(!mkdir(folder,0700)&&!symlink("/",linkpath)&&!mkfifo(pipepath,0600));
 assert(tribes_filesystem_configure(base,root,root,root));
 for(unsigned wide=0;wide<2;wide++){
  cache_file(wide,root);
  create_check(wide,"NewFolder",1,1234);
  create_check(wide,"newFOLDER",0,183);check(wide,"NewFolder",16,1234);
  create_check(wide,"NewFolder/Child",1,1234);check(wide,"newfolder/child",16,1234);
  create_check(wide,"Missing/Child",0,3);create_check(wide,"Asset.bin/Child",0,3);
  create_check(wide,"Asset.bin",0,183);create_check(wide,".",0,183);
  create_check(wide,"link/Forbidden",0,5);create_check(wide,"link",0,183);
  create_check(wide,"bad*name",0,123);
  char created[PATH_MAX];snprintf(created,sizeof created,"%s/NewFolder/Child",root);assert(!rmdir(created));
  snprintf(created,sizeof created,"%s/NewFolder",root);assert(!rmdir(created));
  check(wide,"aSSeT.Bin",32,1234);check(wide,"Folder",16,1234);check(wide,".",16,1234);
  assert(!fchmod(fd,0400));check(wide,"Asset.bin",33,1234);assert(!fchmod(fd,0600));
  assert(!fchflags(fd,UF_HIDDEN));check(wide,"Asset.bin",34,1234);assert(!fchflags(fd,0));
  strcpy((char *)memory+0x20001,"Asset.bin");uint32_t h=tribes_create_file(base,0x20001,0x80000000,0,0,3,0,0);assert(h!=UINT32_MAX);
  check(wide,"Asset.bin",32,1234);assert(tribes_close_handle(h));
  check(wide,"curl-ca-bundle.crt",UINT32_MAX,2);check(wide,"missing/child",UINT32_MAX,3);
  check(wide,"Asset.bin/child",UINT32_MAX,3);check(wide,"bad*name",UINT32_MAX,123);
  check(wide,"",UINT32_MAX,3);check(wide,"link",UINT32_MAX,50);check(wide,"link/tmp",UINT32_MAX,5);check(wide,"pipe",UINT32_MAX,50);
  assert(invoke(wide,0)==UINT32_MAX&&tribes_get_last_error()==87);
  if(wide){uint16_t ch=0x141;memcpy(memory+0x10001,&ch,2);}else memory[0x10001]=0x81;
  assert(invoke(wide,0x10001)==UINT32_MAX&&tribes_get_last_error()==50);
  for(unsigned i=0;i<PATH_MAX;i++){if(wide){uint16_t ch='A';memcpy(memory+0x10001+i*2,&ch,2);}else memory[0x10001+i]='A';}
  assert(invoke(wide,0x10001)==UINT32_MAX&&tribes_get_last_error()==206);
 }
 assert(invoke(1,UINT32_MAX)==UINT32_MAX&&tribes_get_last_error()==87);
 assert(!close(fd)&&!unlink(file)&&!unlink(linkpath)&&!unlink(pipepath)&&!rmdir(folder)&&!rmdir(root));
 puts("PASS: real A/W CREATE_ALWAYS/OPEN_ALWAYS cache writes/truncation/preservation, share-conflict byte preservation, case/confinement/error semantics and directory creation/existing/missing-parent/confinement and mounted-file attributes, directory/readonly/hidden flags, LastError and CPU/stdcall/input preservation, queries during exclusive open, missing leaf/parent, invalid/long/non-ASCII paths, explicit symlink/special-object rejection and intermediate escape denial");return 0;
}
