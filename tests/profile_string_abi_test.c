#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_profile.h"
#include "../port/core/guest_filesystem.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
extern void tbimp_GetProfileStringA(void),tribes_return_to_host(void);
extern void tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t a){switch(a){case 1:return tbimp_GetProfileStringA;case 0xfffff000:return tribes_return_to_host;default:abort();}}
_Noreturn void tribes_unimplemented_dinput(_cpu *c,uint32_t m){(void)c;(void)m;abort();}
static unsigned char memory[16384];
static uint32_t call(const char *section,const char *key,const char *def,uint32_t capacity){
 _cpu c;memset(&c,0xa5,sizeof c);c._pointer_offset=(uintptr_t)memory;c._esp=8192-20;strcpy((char *)memory+100,section);strcpy((char *)memory+200,key);if(def)strcpy((char *)memory+300,def);
 uint32_t a[]={100,200,def?300:0,1000,capacity};memcpy(memory+c._esp,a,sizeof a);memset(memory+900,0x69,600);_cpu before=c;tribes_enter(&c,1);before._eax=c._eax;before._esp=8192;assert(!memcmp(&before,&c,sizeof c));assert(!memcmp(memory+8192-20,a,sizeof a));assert(memory[999]==0x69);if(capacity)assert(memory[1000+c._eax]==0&&memory[1001+c._eax]==0x69);else assert(memory[1000]==0x69);return c._eax;
}
static void expect(const char *section,const char *key,const char *def,const char *want){for(uint32_t cap=1;cap<64;cap++){size_t n=strlen(want);if(n>=cap)n=cap-1;assert(call(section,key,def,cap)==n);assert(!memcmp(memory+1000,want,n));}assert(!call(section,key,def,0));}
static void writefile(const char *path,const char *text){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
int main(void){
 char root[PATH_MAX],windows[PATH_MAX],file[PATH_MAX];const char *tmp=getenv("TMPDIR");if(!tmp)tmp="/tmp/";assert(snprintf(root,sizeof root,"%s/tribes-profile-XXXXXX",tmp)<(int)sizeof root);assert(mkdtemp(root));
 assert(snprintf(windows,sizeof windows,"%s/Windows",root)<(int)sizeof windows);assert(snprintf(file,sizeof file,"%s/Windows/WIN.INI",root)<(int)sizeof file);
 uint64_t base=(uintptr_t)memory;assert(tribes_filesystem_configure(base,root,root,root));assert(tribes_profile_configure_file(base,"C:\\Windows\\win.ini"));assert(!tribes_profile_configure_file(base,"C:\\Other.ini"));
 expect("MSS","MSSDebug","None","None");expect("MSS","MSSDebug",NULL,"");expect("MSS","MSSDebug","Fallback   ","Fallback");
 assert(!mkdir(windows,0700));writefile(file,"; ignored\r\n[Other]\r\nMSSDebug=wrong\r\n[mSs]\r\nMSSDebug = \"debug=log;kept\"\r\nSecondary='Yes'\r\nEmpty=\r\n[Next]\r\nMSSDebug=wrong\r\n");
 expect("MSS","mssdebug","None","debug=log;kept");expect("mss","SECONDARY","None","Yes");expect("mss","Empty","None","");expect("mss","Missing","None","None");expect("absent","MSSDebug","None","None");
 writefile(file,"[MSS]\nMSSDebug=changed\n");expect("MSS","MSSDebug","None","changed");
 assert(!unlink(file));assert(!mkdir(file,0700));assert(call("MSS","MSSDebug","None",64)==0&&tribes_get_last_error()==13);assert(!rmdir(file));assert(!rmdir(windows));assert(!rmdir(root));
 puts("PASS: original emitted GetProfileStringA ABI; real missing/file/changed settings, case-insensitive section/key, quotes/empty/embedded delimiters, null and trimmed defaults, 0..63 capacities, invalid file type, full CPU/flags/stack and output bounds");return 0;
}
