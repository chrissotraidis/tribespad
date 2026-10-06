#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/mman.h>
#include "filetime-golden.h"
extern void tbimp_SystemTimeToFileTime(void),tbimp_FileTimeToSystemTime(void),tbimp_FileTimeToLocalFileTime(void),tbimp_LocalFileTimeToFileTime(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static unsigned char *memory;
static void invoke(unsigned entry,uint32_t input,uint32_t output,unsigned success){
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x100808;
 memcpy(memory+0x100800,&input,4);memcpy(memory+0x100804,&output,4);
 memset(memory+0x1007bc,0x79,64);memset(memory+0x100808,0x79,64);_cpu before=cpu;before._eax=success;
 tribes_set_last_error(123);cpu._esp-=8;tribes_enter(&cpu,entry);
 assert(!memcmp(&cpu,&before,sizeof cpu)&&tribes_get_last_error()==(success?123:87));
 assert(!memcmp(memory+0x100800,&input,4)&&!memcmp(memory+0x100804,&output,4));
 for(unsigned i=0;i<64;i++)assert(memory[0x1007bc+i]==0x79&&memory[0x100808+i]==0x79);
}
static void inverse(uint64_t ticks,const uint16_t *expected,uint32_t input,uint32_t output){
 memset(memory+0x200000,0x79,4096);memset(memory+0x300000,0x79,4096);memcpy(memory+input,&ticks,8);
 invoke(2,input,output,1);assert(!memcmp(memory+output,expected,16));
 if(output==0x300001)assert(memory[0x300000]==0x79&&memory[0x300011]==0x79);
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 for(unsigned p=0x100000;p<=0x300000;p+=0x100000)assert(!mprotect(memory+p,4096,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));assert(tribes_memory_permissions_set(0,UINT64_C(0x100000000),0));
 for(unsigned p=0x100000;p<=0x300000;p+=0x100000)assert(tribes_memory_permissions_set(p,4096,3));
 const void *targets[]={(void *)tbimp_SystemTimeToFileTime,(void *)tbimp_FileTimeToSystemTime,(void *)tbimp_FileTimeToLocalFileTime,(void *)tbimp_LocalFileTimeToFileTime,(void *)tribes_return_to_host};tribes_memory_register_targets(targets,5);
 tribes_memory_external_configure((uintptr_t)memory,UINT64_C(0x100000000));tribes_memory_external_enable();
 uint16_t valid[]={2026,10,65535,6,17,42,46,593};memcpy(memory+0x200001,valid,16);
 if(argc>1&&strchr(argv[1],'-')&&argv[1][0]!='-'){uint32_t input=0x200001,output=0x300001;unsigned entry=1;
  if(!strcmp(argv[1],"input-short"))input=0x200ff9;
  else if(!strcmp(argv[1],"output-short"))output=0x300ffd;
  else if(!strcmp(argv[1],"input-protected")){assert(!mprotect(memory+0x200000,4096,PROT_NONE));assert(tribes_memory_permissions_set(0x200000,4096,0));}
  else if(!strcmp(argv[1],"output-readonly")){assert(!mprotect(memory+0x300000,4096,PROT_READ));assert(tribes_memory_permissions_set(0x300000,4096,1));}
  else if(!strcmp(argv[1],"inverse-input-short")){entry=2;input=0x200ffd;}
  else if(!strcmp(argv[1],"inverse-output-short")){entry=2;memcpy(memory+input,&forward_golden[7],8);output=0x300ff9;}
  else if(!strncmp(argv[1],"to-local-",9)||!strncmp(argv[1],"to-utc-",7)){
   const char *kind=strchr(argv[1]+3,'-')+1;entry=argv[1][3]=='l'?3:4;memcpy(memory+input,&forward_golden[7],8);
   if(!strcmp(kind,"input-short"))input=0x200ffd;
   else if(!strcmp(kind,"output-short"))output=0x300ffd;
   else if(!strcmp(kind,"input-protected")){assert(!mprotect(memory+0x200000,4096,PROT_NONE));assert(tribes_memory_permissions_set(0x200000,4096,0));}
   else if(!strcmp(kind,"output-readonly")){assert(!mprotect(memory+0x300000,4096,PROT_READ));assert(tribes_memory_permissions_set(0x300000,4096,1));}
   else abort();
  }
  invoke(entry,input,output,1);return 99;
 }
 tzset();assert(argc==2);int64_t offset=strtoll(argv[1],NULL,10)*INT64_C(10000000);
 /* Current zoneinfo bias supplied by the Python driver, applied even to a
    January timestamp. Preserve all 100 ns bits, including modulo boundaries. */
 const uint64_t local_cases[]={0,1,UINT64_C(116444736000000001),UINT64_C(134121793665939999),UINT64_C(0x7fffffffffffffff),UINT64_MAX};
 for(unsigned entry=3;entry<=4;entry++)for(unsigned i=0;i<sizeof local_cases/sizeof local_cases[0];i++)for(unsigned j=0;j<3;j++){
  uint32_t input=0x200001,output=j==0?0x300001:j==1?0x300ff8:0x200005;uint64_t ticks=local_cases[i],expected=entry==3?ticks+(uint64_t)offset:ticks-(uint64_t)offset;
  memset(memory+0x200000,0x79,4096);memset(memory+0x300000,0x79,4096);memcpy(memory+input,&ticks,8);invoke(entry,input,output,1);uint64_t actual;memcpy(&actual,memory+output,8);assert(actual==expected);
  if(j==0)assert(memory[0x300000]==0x79&&memory[0x300009]==0x79);
  if(j==0){invoke(entry==3?4:3,output,input,1);assert(!memcmp(memory+input,&ticks,8));}
 }
 for(unsigned entry=3;entry<=4;entry++){
  uint64_t ticks=local_cases[3];memcpy(memory+0x200001,&ticks,8);assert(!mprotect(memory+0x200000,4096,PROT_READ));assert(tribes_memory_permissions_set(0x200000,4096,1));invoke(entry,0x200001,0x300001,1);assert(!mprotect(memory+0x200000,4096,PROT_READ|PROT_WRITE));assert(tribes_memory_permissions_set(0x200000,4096,3));
  invoke(entry,0x200001,0x200001,0);assert(!memcmp(memory+0x200001,&ticks,8));
  memset(memory+0x300000,0x79,4096);invoke(entry,0,0x300001,0);invoke(entry,0xfffffffdu,0x300001,0);invoke(entry,0x200001,0,0);for(unsigned i=0;i<4096;i++)assert(memory[0x300000+i]==0x79);
 }
 for(unsigned i=0;i<sizeof dates/sizeof dates[0];i++){
  uint64_t expected=forward_golden[i];
  for(unsigned j=0;j<3;j++){uint32_t output=j==0?0x300001:j==1?0x300ff8:0x200005;
   memset(memory+0x200000,0x79,4096);memset(memory+0x300000,0x79,4096);memcpy(memory+0x200001,dates[i],16);
   invoke(1,0x200001,output,1);uint64_t actual;memcpy(&actual,memory+output,8);if(actual!=expected)fprintf(stderr,"case=%u output=%u year=%u actual=%llu expected=%llu\n",i,j,dates[i][0],(unsigned long long)actual,(unsigned long long)expected);assert(actual==expected);
   if(j==0)assert(memory[0x300000]==0x79&&memory[0x300009]==0x79);
  }
 }
 for(unsigned i=0;i<sizeof inverse_golden/sizeof inverse_golden[0];i++)for(unsigned j=0;j<3;j++)inverse(inverse_golden[i].ticks,inverse_golden[i].fields,0x200001,j==0?0x300001:j==1?0x300ff0:0x200005);
 memcpy(memory+0x200001,valid,16);assert(!mprotect(memory+0x200000,4096,PROT_READ));assert(tribes_memory_permissions_set(0x200000,4096,1));invoke(1,0x200001,0x300001,1);assert(!memcmp(memory+0x300001,&forward_golden[7],8));assert(!mprotect(memory+0x200000,4096,PROT_READ|PROT_WRITE));assert(tribes_memory_permissions_set(0x200000,4096,3));
 for(unsigned field=0;field<8;field++)if(field!=2){uint16_t v[8];memcpy(v,valid,16);v[field]=field==0?1600:field==1?13:field==3?32:field==4?24:field==5||field==6?60:1000;
  memcpy(memory+0x200001,v,16);memset(memory+0x300000,0x79,4096);invoke(1,0x200001,0x300001,0);for(unsigned i=0;i<4096;i++)assert(memory[0x300000+i]==0x79);
 }
 for(unsigned year=1700;year<=2000;year+=100){uint16_t v[]={year,2,0,29,0,0,0,0};if(year==2000)continue;memcpy(memory+0x200001,v,16);invoke(1,0x200001,0x300001,0);}
 uint16_t too_late[]={30828,1,0,1,0,0,0,0};memcpy(memory+0x200001,too_late,16);invoke(1,0x200001,0x300001,0);
 uint64_t invalid=UINT64_C(0x8000000000000000);memcpy(memory+0x200001,&invalid,8);memset(memory+0x300000,0x79,4096);invoke(2,0x200001,0x300001,0);for(unsigned i=0;i<4096;i++)assert(memory[0x300000+i]==0x79);
 puts("PASS: current local FILETIME bias against Python zoneinfo, inverse 100 ns roundtrips, disjoint/overlapping/readonly/bounded buffers and UTC FILETIME epoch/century/leap/millisecond/upper-limit conversion against independent Python datetime/cycle golden fields, ignored weekday, unaligned/page-end/overlap output, full emitted stdcall CPU/stack preservation and invalid-date/output preservation");
 assert(!munmap(memory,UINT64_C(0x100000000)));return 0;
}
