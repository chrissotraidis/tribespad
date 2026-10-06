#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_modules.h"
#include "../port/core/native_system_providers.h"
#include "../port/core/guest_dinput.h"
#include "../port/core/guest_heap.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/guest_file.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <unistd.h>
#include "providers.inc"
extern void tribes_enter(_cpu *,uint32_t);
extern void *tribes_native_lookup(uint32_t);
_Noreturn void tribes_unimplemented_dinput(void *c,uint32_t n){(void)c;(void)n;abort();}
static uint32_t invoke(_cpu *c,uint32_t target,unsigned count,const uint32_t *args){uint32_t top=c->_esp;unsigned char *m=(void *)(uintptr_t)c->_pointer_offset;c->_esp-=4*count;if(count)memcpy(m+c->_esp,args,4*count);_cpu expected=*c;tribes_enter(c,target);uint32_t answer=c->_eax;expected._eax=answer;expected._esp=top;assert(!memcmp(c,&expected,sizeof expected));return answer;}
int main(void){unsigned char *m=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED&&!mprotect(m+0x100000,0x10000,PROT_READ|PROT_WRITE));uint64_t base=(uintptr_t)m;
 assert(tribes_dinput_configure(base));assert(tribes_system_providers_configure(base));unsigned lookups=0;
 for(unsigned i=0;i<sizeof providers/sizeof providers[0];++i){const provider_case *p=&providers[i];assert(p->image!=TB_DINPUT_MODULE);strcpy((char *)m+0x100000,p->name);assert(tribes_get_module_handle(base,0x100000)==(p->initial_references?p->image:0));assert(tribes_load_library(base,0x100000)==p->image);
  for(unsigned j=0;j<p->count;++j){const export_case *e=&p->exports[j];strcpy((char *)m+0x100100,e->name);uint32_t address=tribes_get_proc_address(base,p->image,0x100100);assert(address==e->token);assert(tribes_native_lookup(address)==e->function&&tribes_system_native_lookup(address)==e->function);++lookups;}
  strcpy((char *)m+0x100100,"DefinitelyNotAnExport");assert(!tribes_get_proc_address(base,p->image,0x100100)&&tribes_get_last_error()==127);
  assert(tribes_free_library(base,p->image));assert(tribes_get_module_handle(base,0x100000)==(p->initial_references?p->image:0));
 }
 assert(!tribes_system_native_lookup(0xfffb0001)&&!tribes_native_lookup(0xfffbfff0));
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x10f003;
 /* Actual dynamically found wrappers, not C calls or address-only fixtures. */
 strcpy((char *)m+0x100000,"kernel32.dll");uint32_t module=tribes_load_library(base,0x100000),targets[4];const char *names[]={"TlsAlloc","TlsSetValue","TlsGetValue","TlsFree"};
 for(unsigned i=0;i<4;++i){strcpy((char *)m+0x100100,names[i]);targets[i]=tribes_get_proc_address(base,module,0x100100);assert(targets[i]);}
 for(unsigned i=0;i<64;++i){uint32_t index=invoke(&cpu,targets[0],0,NULL);assert(index!=UINT32_MAX);uint32_t set[]={index,0x93467123u+i};assert(invoke(&cpu,targets[1],2,set)==1);assert(invoke(&cpu,targets[2],1,&index)==set[1]);assert(invoke(&cpu,targets[3],1,&index)==1);}
 assert(tribes_free_library(base,module));
 strcpy((char *)m+0x100000,"bcrypt.dll");module=tribes_load_library(base,0x100000);assert(module==0x20030000);
 strcpy((char *)m+0x100100,"BCryptGenRandom");uint32_t random=tribes_get_proc_address(base,module,0x100100);assert(random);
 unsigned char before[80];memset(before,0x5e,sizeof before);memcpy(m+0x100200,before,sizeof before);
 uint32_t random_args[]={0,0x100208,64,2};tribes_set_last_error(891);
 assert(!invoke(&cpu,random,4,random_args)&&tribes_get_last_error()==891);
 assert(!memcmp(m+0x100200,before,8)&&!memcmp(m+0x100248,before+72,8)&&memcmp(m+0x100208,before+8,64));
 memcpy(before,m+0x100200,sizeof before);random_args[3]=0;
 assert(invoke(&cpu,random,4,random_args)==0xc0000008&&tribes_get_last_error()==891&&!memcmp(m+0x100200,before,sizeof before));
 assert(tribes_free_library(base,module)&&tribes_get_module_handle(base,0x100000)==module);
 strcpy((char *)m+0x100000,"ntdll.dll");module=tribes_load_library(base,0x100000);assert(module==0x20000000);
 strcpy((char *)m+0x100100,"NtQueryVolumeInformationFile");uint32_t query=tribes_get_proc_address(base,module,0x100100);assert(query);
 char path[]="/tmp/tribes-provider-volume-XXXXXX";int fd=mkstemp(path);assert(fd>=0&&!unlink(path));
 struct statfs fs;assert(!fstatfs(fd,&fs));uint32_t h=tribes_file_fd_adopt(fd,0,7);assert(h);
 uint32_t volume_args[]={h,0x100300,0x100310,8,4};tribes_set_last_error(891);
 assert(!invoke(&cpu,query,5,volume_args)&&tribes_get_last_error()==891);
 uint32_t expected_io[]={0,8},expected_device[]={fs.f_flags&MNT_LOCAL?8:0x14,(fs.f_flags&MNT_RDONLY?2:0)|(fs.f_flags&MNT_LOCAL?0:0x10)};
 assert(!memcmp(m+0x100300,expected_io,8)&&!memcmp(m+0x100310,expected_device,8));assert(tribes_close_handle(h));
 char root[]="/tmp/tribes-provider-file-XXXXXX",named[PATH_MAX];assert(mkdtemp(root));
 snprintf(named,sizeof named,"%s/certificate.crt",root);FILE *stream=fopen(named,"w");assert(stream&&fwrite("CERT",1,4,stream)==4&&!fclose(stream));
 assert(tribes_filesystem_configure(base,root,root,root));strcpy((char *)m+0x100400,"certificate.crt");
 h=tribes_create_file(base,0x100400,0x10180,7,0,3,0x02200000,0);assert(h!=UINT32_MAX);
 strcpy((char *)m+0x100100,"NtQueryInformationFile");query=tribes_get_proc_address(base,module,0x100100);assert(query==0xfffb1230);
 uint32_t file_args[]={h,0x100300,0x100500,104,18};tribes_set_last_error(891);
 assert(invoke(&cpu,query,5,file_args)==0x80000005&&tribes_get_last_error()==891);
 uint32_t expected_partial[]={0x80000005,104},size=0;memcpy(&size,m+0x100530,4);
 assert(!memcmp(m+0x100300,expected_partial,8)&&size==4&&!memcmp(m+0x100564,"\\\0T\0",4));
 assert(tribes_close_handle(h)&&!unlink(named)&&!rmdir(root));
 assert(tribes_free_library(base,module)&&tribes_get_module_handle(base,0x100000)==module);
 assert(!munmap(m,UINT64_C(0x100000000)));
 printf("PASS: %u finite provider exports; disjoint DirectInput identity; missing exports/refcounts; 64 dynamic TLS cycles, system-preferred BCrypt RNG and actual NT file/volume queries through real ARM64 wrappers with full CPU/stack preservation\n",lookups);return 0;}
