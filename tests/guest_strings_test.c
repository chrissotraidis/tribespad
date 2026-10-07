#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t),tribes_return_to_host(void),tbimp_lstrcatA(void),tbimp_lstrcpyA(void),tbimp_lstrlenA(void),tbimp_SDL_strrchr(void);
void *tribes_lookup(uint32_t a){switch(a){case 1:return tbimp_lstrcatA;case 2:return tbimp_lstrcpyA;case 3:return tbimp_lstrlenA;case 4:return tbimp_SDL_strrchr;case 0xfffff000:return tribes_return_to_host;default:abort();}}
static unsigned char *m;
static uint32_t call(unsigned target,unsigned n,uint32_t *args){_cpu c={0};c._pointer_offset=(uintptr_t)m;c._esp=0x180000-4*n;c._eax=123;c._eflags=0x246;memcpy(m+c._esp,args,4*n);_cpu before=c;tribes_enter(&c,target);before._eax=c._eax;before._esp=target==4?0x180000-4*n:0x180000;assert(!memcmp(&before,&c,sizeof c));return c._eax;}
int main(int argc,char **argv){
 m=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED&&!mprotect(m+0x100000,0x100000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure((uintptr_t)m,UINT64_C(0x100000000)));strcpy((char *)m+0x101000,"Miles ");strcpy((char *)m+0x102000,"audio\x80\xff");uint32_t a[]={0x101000,0x102000};
 if(argc>1&&!strcmp(argv[1],"readonly"))assert(tribes_memory_permissions_set(0x101000,4096,1));
 if(argc>1&&!strcmp(argv[1],"unreadable"))assert(tribes_memory_permissions_set(0x102000,4096,0));
 if(argc>1&&!strncmp(argv[1],"search-",7)){
  uint32_t q[]={0x103fff,'.'};m[q[0]]='.';
  if(!strcmp(argv[1],"search-null"))q[0]=0;
  else if(!strcmp(argv[1],"search-wrap")){assert(!mprotect(m+0xffffc000,16384,PROT_READ|PROT_WRITE));m[0xffffffff]='.';q[0]=0xffffffff;}
  else assert(tribes_memory_permissions_set(0x104000,4096,0));
  call(4,2,q);abort();
 }
 tribes_set_last_error(123);assert(call(1,2,a)==a[0]&&tribes_get_last_error()==123);assert(!strcmp((char *)m+a[0],"Miles audio\x80\xff"));assert(call(3,1,a)==13);assert(call(2,2,a)==a[0]&&!strcmp((char *)m+a[0],"audio\x80\xff"));m[a[1]]=0;assert(call(1,2,a)==a[0]&&call(3,1,a)==7);assert(call(2,2,a)==a[0]&&!m[a[0]]);a[0]=0;assert(!call(3,1,a));assert(!call(2,2,a)&&tribes_get_last_error()==87);
 const char sample[]="prefs.WASD.\x80\xff.cs";uint32_t q[]={0x103000,0};memcpy(m+q[0],sample,sizeof sample);
 assert(tribes_memory_permissions_set(0x103000,4096,1));
 for(int c=-256;c<=511;c++){
  const char *expected=strrchr(sample,c);q[1]=(uint32_t)c;tribes_set_last_error(456);
  assert(call(4,2,q)==(expected?q[0]+(uint32_t)(expected-sample):0)&&tribes_get_last_error()==456);
 }
 assert(tribes_memory_permissions_set(0x103000,4096,3));m[0x103ffe]='.';m[0x103fff]=0;
 assert(tribes_memory_permissions_set(0x104000,4096,0));q[0]=0x103ffe;q[1]='.';assert(call(4,2,q)==q[0]);q[1]=0;assert(call(4,2,q)==0x103fff);q[0]=0x103fff;assert(call(4,2,q)==q[0]);q[1]='.';assert(call(4,2,q)==0);
 puts("PASS: Win32 strings and SDL reverse search, 768 libc comparisons, readonly and exact-page terminators, cdecl stack and CPU/LastError preservation");return 0;
}
